// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"
#include "page.h"

static void freerange(void *pa_start, void *pa_end);
static void kfree1(void* pa, int decrease_ref);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.
extern char end_pg[];   // first page-aligned address after kernel.
                        // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

// The (physical) page descriptor table.
// Indexed by the page frame number.
struct page pages[NPAGES];

// Convert a physical address to a page frame number.
// Pre-condition: end_pg <= pa < PHYSTOP
uint64
pa_to_pfn(uint64 pa)
{
  if ((pa % PGSIZE) != 0 || pa < (uint64)end_pg || pa > (uint64)PHYSTOP)
    panic("pa_to_pfn");

  return (pa - (uint64)end_pg) >> 12;
}

void
kinit()
{
  int i;
  struct page *pd;

  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);

  for (i=0; i<NPAGES; ++i) {
    pd = &pages[i];
    initlock(&pd->lock, "page");
    pd->ref = 0;
  }
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree1(p, 0);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void* pa)
{
  kfree1(pa, 1);
}

void
kfree1(void *pa, int decrese_ref)
{
  struct run *r;

  //printf("kree1: pa=%p\n", pa);

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  if (decrese_ref) {
    uint64 pfn = pa_to_pfn((uint64)pa);
    struct page *pd = &pages[pfn];

    acquire(&pd->lock);

    if (pd->ref <= 0)
      panic("kfree: page has zero or negative ref count");
    
    if (--pd->ref > 0) {
      printf("kfree: pa=0x%lx, ref count becomes %d\n", (uint64)pa, pd->ref);
    }
    
    release(&pd->lock);
  } else {
    uint64 pfn = pa_to_pfn((uint64)pa);
    struct page *pd = &pages[pfn];

    if (pd->ref != 0)
      panic("kfree for freerange: page has non-zero ref count");    
  }

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  struct page *pd;
  uint64 pfn;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r) {
    pfn = pa_to_pfn((uint64)r);
    pd = &pages[pfn];

    acquire(&pd->lock);
    if (pd->ref)
      panic("kalloc: non-zero ref count on free page");
    ++pd->ref;
    release(&pd->lock);

    memset((char*)r, 5, PGSIZE); // fill with junk

    //printf("kalloc: pa=%p\n", r);
  }
  return (void*)r;
}
