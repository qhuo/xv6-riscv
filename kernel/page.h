// Physical page descriptor.
struct page {
  struct spinlock lock;
  uint ref;
};

extern struct page pages[NPAGES];
