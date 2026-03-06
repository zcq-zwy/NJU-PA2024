#include <am.h>
#include <klib.h>
#include <klib-macros.h>

#if !defined(__ISA_NATIVE__) || defined(__NATIVE_USE_KLIB__)
static unsigned long int next = 1;

int rand(void) {
  // RAND_MAX assumed to be 32767
  next = next * 1103515245 + 12345;
  return (unsigned int)(next / 65536) % 32768;
}

void srand(unsigned int seed) {
  next = seed;
}

int abs(int x) {
  return (x < 0 ? -x : x);
}

int atoi(const char *nptr) {
  int x = 0;
  while (*nptr == ' ') { nptr++; }
  while (*nptr >= '0' && *nptr <= '9') {
    x = x * 10 + *nptr - '0';
    nptr++;
  }
  return x;
}

#if !(defined(__ISA_NATIVE__) && defined(__NATIVE_USE_KLIB__))
static uintptr_t brk = 0;
#endif

void *malloc(size_t size) {
  // On native, malloc() will be called during initializaion of C runtime.
  // Therefore do not call panic() here, else it will yield a dead recursion:
  //   panic() -> putchar() -> (glibc) -> malloc() -> panic()
#if !(defined(__ISA_NATIVE__) && defined(__NATIVE_USE_KLIB__))
  if (size == 0) return NULL;

  if (brk == 0) {
    brk = (uintptr_t)heap.start;
  }

  // Keep returned address and allocation size 8-byte aligned.
  uintptr_t ret = (brk + 7u) & ~((uintptr_t)7u);
  size = (size + 7u) & ~((size_t)7u);

  uintptr_t next_brk = ret + size;
  if (next_brk < ret || next_brk > (uintptr_t)heap.end) {
    return NULL;
  }

  brk = next_brk;
  return (void *)ret;
#endif
  return NULL;
}

void free(void *ptr) {
  (void)ptr;
}

#endif
