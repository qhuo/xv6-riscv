#include "types.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

struct dtb_header {
    uint32 magic;
    uint32 total_size;
    uint32 struct_offset;
    uint32 strings_offset;
    uint32 mem_map_offset;
    uint32 version;
    uint32 last_comp_version;
    uint32 boot_cpu_id;
    uint32 strings_size;
    uint32 struct_size;
};

void dtbinit(const char* ptr)
{
    int i;

    printf("processing the Device Tree Blob...\n");

    printf("DTB ptr=%p\n", ptr);

    for (i=0; i<4; ++i) {
        printf("magic byte: %x\n", ptr[i]);
    }

    printf("done\n\n");
}
