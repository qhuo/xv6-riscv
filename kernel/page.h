// Physical page descriptor.
struct page_t {
  struct spinlock lock;
  uint ref;
};

extern struct page_t page[NPAGES];
