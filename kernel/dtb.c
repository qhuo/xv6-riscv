#include "types.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

struct dtb_header {
  uint32 magic;
  uint32 total_size;
  uint32 off_dt_struct;
  uint32 off_dt_strings;
  uint32 off_mem_rsvmap;
  uint32 version;
  uint32 last_comp_version;
  uint32 boot_cpuid_phys;
  uint32 size_dt_strings;
  uint32 size_dt_struct;
} dtb_header;

#define DTB_MAGIC (0xD00DFEED)
// Only support this one version.
#define DTB_VERSION_EXPECTED (17)

struct dt_token {
  uint32 type;
};

#define DT_BEGIN_NODE (1)
#define DT_END_NODE (2)
#define DT_PROP (3)
#define DT_NOP (4)
#define DT_END (9)

// Data structure for reading DTB data with bounds checking. 
struct dtb_reader {
  const char* base; // base address
  uint32 index;     // index of the next char to read
  uint32 end;       // one past the last allowed index to read.
  uint32 strings_base;  // base offset of the stringe table.
  uint32 strings_end;   // one past the last valid index in the strings table.
};

// Update to the new read range, [index, end),
// does not allow going back.
static void reader_advance(struct dtb_reader* r, uint32 index, uint32 end)
{
  if (index < r->index || index < r->end)
    panic("dtb: reader_advance: bad index");
  if (end < index)
    panic("dtb: reader_advance: range error");

  r->index = index;
  r->end = end;
}

static void reader_advance_to_aligned_4b(struct dtb_reader* r)
{
  uint32 i = r->index;

  while (i & 3)
    ++i;

  r->index = i;
}

static void reader_skip(struct dtb_reader* r, uint32 n)
{
  uint32 i;
  uint8 ch;

  if (r->index >= r->end || r->index + n > r->end)
    panic("reader_skip out of bound");

  for (i=0; i<n; ++i) {
    ch = r->base[r->index++];
    if (ch >= ' ' && ch <= '~') {
      consputc(ch);
    } else {
      printf("\\%x", ch);
    }
  }
}

// Read a uint32 from the DTB read range.
// Convert from BE to host byte order.
static uint32 read_uint32(struct dtb_reader* r)
{
  if (r->index >= r->end || r->index + 4 > r->end)
    panic("dtb: read_uint32: out of bound");

  if (r->index & 3)
    panic("dtb: read_uint32: unaligned address");

  const uint32 b0 = (uint32) r->base[r->index++];
  const uint32 b1 = (uint32) r->base[r->index++];
  const uint32 b2 = (uint32) r->base[r->index++];
  const uint32 b3 = (uint32) r->base[r->index++];

  return (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
}

// Read a uint64 from the DTB read range.
// Convert from BE to host byte order.
static uint64 read_uint64(struct dtb_reader* r)
{
  const uint32 w0 = read_uint32(r);
  const uint32 w1 = read_uint32(r);

  return (((uint64) w0) << 32) | ((uint64) w1);
}

static const char* read_str(struct dtb_reader* r)
{
  const char* p = &r->base[r->index];
  const char* const p0 = p;
  const char* z = &r->base[r->end];

  for (; p<z && *p; ++p)
    ;

  if (p >= z)
    panic("dtb: read_str");

  // Found a C-string [p0, p), where *p == '\0'
  r->index += p - p0 + 1;

  return p0;
}

// Similar to read_str(), but read from the strings table.
// (a) use the offset passed in.
// (b) use the offset and limit of the strings table.
static const char* read_str_from_strings_table(struct dtb_reader* r, uint32 offset)
{
  const char* p = &r->base[r->strings_base + offset];
  const char* const p0 = p;
  const char* z = &r->base[r->strings_end];

  for (; p<z && *p; ++p)
    ;

  if (p >= z)
    panic("dtb: read_str");

  // Found a C-string [p0, p), where *p == '\0'

  return p0;
}

static void read_header_field(struct dtb_reader* r, const char* name, uint32* dst_ptr)
{
  uint32 value;

  printf("%s ... ", name);

  value = read_uint32(r);
  *dst_ptr = value;

  printf("0x%x\n", value);
}

static void print_indent(int indent)
{
    for (int i=0; i<indent; ++i)
        printf("    "); // 4 space
}

static void read_dt_token(struct dtb_reader* r, struct dt_token* token)
{
  static int indent = 0;

  reader_advance_to_aligned_4b(r);
  token->type = read_uint32(r);

  switch (token->type) {    
    default:
      printf("read_dt_token: type=%d\n", token->type);
      panic("dtb: read_dt_token: unexpected token type");
      break;
    
    case DT_BEGIN_NODE: {
      const char* name = read_str(r);
      print_indent(indent);
      printf("%s/\n", name);
      ++indent;
      break;
    }

    case DT_PROP: {
      const uint32 len = read_uint32(r);
      const uint32 nameoff = read_uint32(r);
      const char* name = read_str_from_strings_table(r, nameoff);
      print_indent(indent);
      printf("%s: [%u bytes] ", name, len);
      reader_skip(r, len);
      printf("\n");
      break;
    }

    case DT_END_NODE:
      if (--indent < 0)
        panic("DT_END_NODE");
      break;

    case DT_NOP:
    case DT_END:
      break;
  }  
}

void dtbinit(const char* dtb_ptr)
{
  struct dtb_reader r;
  int n;

  printf("Processing the Device Tree Blob...\n");
  printf("DTB ptr: %p\n\n", dtb_ptr);

  r.base = dtb_ptr;

  printf("Reading DTB header fields...\n");

  r.index = 0;
  r.end = sizeof(uint32) * 7;   // DTB header size (version agnostic)
  r.strings_end = 0;    // To be updated later.

  read_header_field(&r, "magic", &dtb_header.magic);
  if (dtb_header.magic != DTB_MAGIC)
    panic("DTB header magic error");
      
  read_header_field(&r, "total_size", &dtb_header.total_size);
  read_header_field(&r, "off_dt_struct", &dtb_header.off_dt_struct);
  read_header_field(&r, "off_dt_strings", &dtb_header.off_dt_strings);
  read_header_field(&r, "off_mem_rsvmap", &dtb_header.off_mem_rsvmap);
  read_header_field(&r, "version", &dtb_header.version);
  read_header_field(&r, "last_comp_version", &dtb_header.last_comp_version);
  
  if (dtb_header.version != DTB_VERSION_EXPECTED)
    panic("DTB header version error");

  r.end = sizeof(uint32) * 10;   // DTB header size (version 17)

  read_header_field(&r, "boot_cpuid_phys", &dtb_header.boot_cpuid_phys);
  read_header_field(&r, "size_dt_strings", &dtb_header.size_dt_strings);
  read_header_field(&r, "size_dt_struct", &dtb_header.size_dt_struct);

  // TODO: verifications:
  r.strings_base = dtb_header.off_dt_strings;
  r.strings_end = dtb_header.off_dt_strings + dtb_header.size_dt_strings;

  printf("\nProcessing the memory reservation map...\n");
  reader_advance(&r, dtb_header.off_mem_rsvmap, dtb_header.off_dt_struct);
  
  for (n=0; ; ++n) {
    uint64 address, size;

    address = read_uint64(&r);
    size = read_uint64(&r);

    if (address == 0 && size == 0)
      break;
    
    printf("Memory reserved: address=0x%lx, size=0x%lx\n", address, size);
  }

  printf("Read %d entries.\n\n", n);

  printf("Processing the device tree...\n");
  reader_advance(&r, dtb_header.off_dt_struct, dtb_header.off_dt_struct + dtb_header.size_dt_struct);
  
  for (n=0; ; ++n) {
    struct dt_token token;

    read_dt_token(&r, &token);

    if (token.type == DT_END)
      break;
  }

  printf("Read %d entries.\n\n", n);
}

