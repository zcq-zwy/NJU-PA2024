// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

#define NREFLOCK 16

struct {
  struct spinlock lock[NCPU];
  struct run *freelist[NCPU];
  struct spinlock reflock[NREFLOCK];
  int refcnt[(PHYSTOP - KERNBASE) / PGSIZE];
} kmem;

static char *kmem_lock_names[NCPU] = {
  "kmem0", "kmem1", "kmem2", "kmem3",
  "kmem4", "kmem5", "kmem6", "kmem7",
};
static char *kref_lock_names[NREFLOCK] = {
  "kref0", "kref1", "kref2", "kref3",
  "kref4", "kref5", "kref6", "kref7",
  "kref8", "kref9", "kref10", "kref11",
  "kref12", "kref13", "kref14", "kref15",
};

static int
pa2idx(uint32 pa)
{
  return (pa - KERNBASE) / PGSIZE;
}

static int
kmem_cpuid(void)
{
  int id;

  push_off();
  id = cpuid();
  pop_off();
  return id;
}

static void
kmem_push(int cpu, struct run *r)
{
  acquire(&kmem.lock[cpu]);
  r->next = kmem.freelist[cpu];
  kmem.freelist[cpu] = r;
  release(&kmem.lock[cpu]);
}

static struct run *
kmem_pop(int cpu)
{
  struct run *r;

  acquire(&kmem.lock[cpu]);
  r = kmem.freelist[cpu];
  if(r)
    kmem.freelist[cpu] = r->next;
  release(&kmem.lock[cpu]);
  return r;
}

static struct spinlock *
kmem_reflock(int idx)
{
  return &kmem.reflock[idx % NREFLOCK];
}

static void
kmem_setref(int idx, int ref)
{
  struct spinlock *lk;

  lk = kmem_reflock(idx);
  acquire(lk);
  kmem.refcnt[idx] = ref;
  release(lk);
}

static int
kmem_putref(int idx)
{
  struct spinlock *lk;
  int ref;

  lk = kmem_reflock(idx);
  acquire(lk);
  if(kmem.refcnt[idx] < 1)
    panic("kfree ref");
  kmem.refcnt[idx]--;
  ref = kmem.refcnt[idx];
  release(lk);
  return ref;
}

void
kinit()
{
  int i;

  for(i = 0; i < NCPU; i++){
    initlock(&kmem.lock[i], kmem_lock_names[i]);
    kmem.freelist[i] = 0;
  }
  for(i = 0; i < NREFLOCK; i++)
    initlock(&kmem.reflock[i], kref_lock_names[i]);
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  int cpu;

  cpu = 0;
  p = (char*)PGROUNDUP((uint32)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE) {
    memset(p, 1, PGSIZE);
    kmem.refcnt[pa2idx((uint32)p)] = 0;
    kmem_push(cpu, (struct run*)p);
    cpu = (cpu + 1) % NCPU;
  }
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  int idx;

  if(((uint32)pa % PGSIZE) != 0 || (char*)pa < end || (uint32)pa >= PHYSTOP)
    panic("kfree");

  idx = pa2idx((uint32)pa);
  if(kmem_putref(idx) > 0)
    return;

  kmem_push(kmem_cpuid(), (struct run*)pa);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  int cpu, other;

  cpu = kmem_cpuid();
  r = kmem_pop(cpu);
  if(r == 0){
    for(other = 0; other < NCPU; other++){
      if(other == cpu)
        continue;
      r = kmem_pop(other);
      if(r)
        break;
    }
  }

  if(r){
    kmem_setref(pa2idx((uint32)r), 1);
  }
  return (void*)r;
}

uint32
kfreemem(void)
{
  struct run *r;
  uint32 free_pages = 0;
  int cpu;

  for(cpu = 0; cpu < NCPU; cpu++){
    acquire(&kmem.lock[cpu]);
    for(r = kmem.freelist[cpu]; r; r = r->next)
      free_pages++;
    release(&kmem.lock[cpu]);
  }

  return free_pages * PGSIZE;
}

void
kaddref(void *pa)
{
  int idx;
  struct spinlock *lk;

  idx = pa2idx((uint32)pa);
  lk = kmem_reflock(idx);
  acquire(lk);
  kmem.refcnt[idx]++;
  release(lk);
}

int
kgetref(void *pa)
{
  int idx, ref;
  struct spinlock *lk;

  idx = pa2idx((uint32)pa);
  lk = kmem_reflock(idx);
  acquire(lk);
  ref = kmem.refcnt[idx];
  release(lk);
  return ref;
}
