#include <am.h>
#include <nemu.h>
#include <klib.h>

#define MSTATUS_MPIE (1u << 7)
#define CONTEXT_USER 1u

static AddrSpace kas = {};
static void* (*pgalloc_usr)(int) = NULL;
static void (*pgfree_usr)(void*) = NULL;
static int vme_enable = 0;

static Area segments[] = {
  NEMU_PADDR_SPACE
};

#define USER_SPACE RANGE(0x40000000, 0x80000000)

static inline void set_satp(void *pdir) {
  uintptr_t mode = 1ul << (__riscv_xlen - 1);
  asm volatile("csrw satp, %0" : : "r"(mode | ((uintptr_t)pdir >> 12)));
}

static inline uintptr_t get_satp() {
  uintptr_t satp;
  asm volatile("csrr %0, satp" : "=r"(satp));
  return satp << 12;
}

bool vme_init(void* (*pgalloc_f)(int), void (*pgfree_f)(void*)) {
  pgalloc_usr = pgalloc_f;
  pgfree_usr = pgfree_f;

  kas.ptr = pgalloc_f(PGSIZE);

  for (int i = 0; i < LENGTH(segments); i ++) {
    void *va = segments[i].start;
    for (; va < segments[i].end; va += PGSIZE) {
      map(&kas, va, va, 0);
    }
  }

  set_satp(kas.ptr);
  vme_enable = 1;

  return true;
}

void protect(AddrSpace *as) {
  PTE *updir = (PTE *)(pgalloc_usr(PGSIZE));
  as->ptr = updir;
  as->area = USER_SPACE;
  as->pgsize = PGSIZE;
  memcpy(updir, kas.ptr, PGSIZE);
}

void unprotect(AddrSpace *as) {
}

void __am_get_cur_as(Context *c) {
  c->pdir = (vme_enable ? (void *)get_satp() : NULL);
}

void __am_switch(Context *c) {
  if (vme_enable && c->pdir != NULL) {
    set_satp(c->pdir);
  }
}

void map(AddrSpace *as, void *va, void *pa, int prot) {
  assert(as != NULL);
  assert(as->ptr != NULL);
  assert((uintptr_t)va % PGSIZE == 0);
  assert((uintptr_t)pa % PGSIZE == 0);
  if (as != &kas) {
    assert(IN_RANGE(va, as->area));
  }

  uintptr_t vaddr = (uintptr_t)va;
  PTE *pgdir = (PTE *)as->ptr;
  int vpn1 = (vaddr >> 22) & 0x3ff;
  int vpn0 = (vaddr >> 12) & 0x3ff;

  if (!(pgdir[vpn1] & PTE_V)) {
    PTE *pt = (PTE *)pgalloc_usr(PGSIZE);
    assert(pt != NULL);
    pgdir[vpn1] = ((uintptr_t)pt >> 12 << 10) | PTE_V;
  }

  PTE *pt = (PTE *)(((uintptr_t)pgdir[vpn1] >> 10) << 12);
  PTE perm = PTE_V | PTE_R | PTE_W | PTE_X | PTE_A | PTE_D;
  if (as != &kas) perm |= PTE_U;
  pt[vpn0] = ((uintptr_t)pa >> 12 << 10) | perm;
  (void)prot;
}

Context *ucontext(AddrSpace *as, Area kstack, void *entry) {
  uintptr_t sp = (uintptr_t)kstack.end;
  sp &= ~(uintptr_t)0xf;
  Context *c = (Context *)(sp - sizeof(Context));
  *c = (Context) { 0 };

  c->mstatus = MSTATUS_SUM | MSTATUS_MXR | MSTATUS_MPIE;
  c->mepc = (uintptr_t)entry;
  c->pdir = (as == NULL ? NULL : as->ptr);
  c->np = CONTEXT_USER;
  return c;
}
