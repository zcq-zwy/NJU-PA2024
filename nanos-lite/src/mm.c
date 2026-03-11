#include <memory.h>
#include <proc.h>

static void *pf = NULL;
static void *pf_start = NULL;

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
#ifdef HAS_VME
  assert(current != NULL);
  if (brk > current->max_brk) {
    uintptr_t start = ROUNDUP(current->max_brk, PGSIZE);
    uintptr_t end = ROUNDUP(brk, PGSIZE);
    for (uintptr_t va = start; va < end; va += PGSIZE) {
      void *pa = new_page(1);
      map(&current->as, (void *)va, pa, MMAP_READ | MMAP_WRITE);
    }
    current->max_brk = brk;
  }
#else
  (void)brk;
#endif
  return 0;
}

void init_mm() {
  pf = (void *)ROUNDUP(heap.start, PGSIZE);
  pf_start = pf;
  Log("free physical pages starting from %p", pf);

#ifdef HAS_VME
  vme_init(pg_alloc, free_page);
#endif
}

size_t mm_used_bytes(void) {
  return (size_t)((uintptr_t)pf - (uintptr_t)pf_start);
}

size_t mm_total_bytes(void) {
  return (size_t)((uintptr_t)heap.end - (uintptr_t)pf_start);
}
