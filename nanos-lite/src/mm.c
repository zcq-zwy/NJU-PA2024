#include <memory.h>

static void *pf = NULL;

void* new_page(size_t nr_page) {
  void *p = pf;
  pf += nr_page * PGSIZE;
  assert(pf <= (void *)heap.end);
  memset(p, 0, nr_page * PGSIZE);
  return p;
}

#ifdef HAS_VME
static void* pg_alloc(int n) {
  assert(n > 0 && n % PGSIZE == 0);
  return new_page(n / PGSIZE);
}
#endif

void free_page(void *p) {
  panic("not implement yet");
}

int mm_brk(uintptr_t brk) {
  return 0;
}

void init_mm() {
  pf = (void *)ROUNDUP(heap.start, PGSIZE);
  Log("free physical pages starting from %p", pf);

#ifdef HAS_VME
  vme_init(pg_alloc, free_page);
#endif
}
