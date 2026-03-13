// Mutual exclusion spin locks.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "proc.h"
#include "defs.h"

void
initlock(struct spinlock *lk, char *name)
{
  lk->name = name;
  lk->locked = 0;
  lk->cpu = 0;
}

// Acquire the lock.
// Loops (spins) until the lock is acquired.
void
acquire(struct spinlock *lk)
{
  push_off(); // disable interrupts to avoid deadlock.
  if(holding(lk))
    panic("acquire");

  // On RISC-V, sync_lock_test_and_set turns into an atomic swap:
  //   a5 = 1
  //   s1 = &lk->locked
  //   amoswap.w.aq a5, a5, (s1)
  while(__sync_lock_test_and_set(&lk->locked, 1) != 0)
    ;

  // Tell the C compiler and the processor to not move loads or stores
  // past this point, to ensure that the critical section's memory
  // references happen after the lock is acquired.
  __sync_synchronize();

  // Record info about lock acquisition for holding() and debugging.
  lk->cpu = mycpu();
}

// Release the lock.
void
release(struct spinlock *lk)
{
  if(!holding(lk))
    panic("release");

  lk->cpu = 0;

  // Tell the C compiler and the CPU to not move loads or stores
  // past this point, to ensure that all the stores in the critical
  // section are visible to other CPUs before the lock is released.
  // On RISC-V, this turns into a fence instruction.
  __sync_synchronize();

  // Release the lock, equivalent to lk->locked = 0.
  // This code doesn't use a C assignment, since the C standard
  // implies that an assignment might be implemented with
  // multiple store instructions.
  // On RISC-V, sync_lock_release turns into an atomic swap:
  //   s1 = &lk->locked
  //   amoswap.w zero, zero, (s1)
  __sync_lock_release(&lk->locked);

  pop_off();
}

// Check whether this cpu is holding the lock.
int
holding(struct spinlock *lk)
{
  int r;
  push_off();
  r = (lk->locked && lk->cpu == mycpu());
  pop_off();
  return r;
}

// push_off/pop_off are like intr_off()/intr_on() except that they are matched:
// it takes two pop_off()s to undo two push_off()s.  Also, if interrupts
// are initially off, then push_off, pop_off leaves them off.

void
push_off(void)
{
  int old = intr_get();

  intr_off();
  if(mycpu()->noff == 0)
    mycpu()->intena = old;
  mycpu()->noff += 1;
}

void
pop_off(void)
{
  struct cpu *c = mycpu();
  if(intr_get())
    panic("pop_off - interruptible");
  c->noff -= 1;
  if(c->noff < 0)
    panic("pop_off");
  if(c->noff == 0 && c->intena)
    intr_on();
}

static void
rwlock_wait(struct rwspinlock *lk, int writer)
{
  for(;;){
    acquire(&lk->lock);
    if(writer){
      if(lk->writer == 0 && lk->readers == 0){
        lk->pending_writers--;
        lk->writer = 1;
        lk->cpu = mycpu();
        release(&lk->lock);
        return;
      }
    } else {
      if(lk->writer == 0 && lk->pending_writers == 0){
        lk->readers++;
        release(&lk->lock);
        return;
      }
    }
    release(&lk->lock);
  }
}

void
initrwlock(struct rwspinlock *lk, char *name)
{
  initlock(&lk->lock, name);
  lk->readers = 0;
  lk->writer = 0;
  lk->pending_writers = 0;
  lk->name = name;
  lk->cpu = 0;
}

void
rwinitlock(struct rwspinlock *lk, char *name)
{
  initrwlock(lk, name);
}

void
acquireread(struct rwspinlock *lk)
{
  rwlock_wait(lk, 0);
}

void
read_acquire(struct rwspinlock *lk)
{
  acquireread(lk);
}

void
releaseread(struct rwspinlock *lk)
{
  acquire(&lk->lock);
  if(lk->readers < 1)
    panic("releaseread");
  lk->readers--;
  release(&lk->lock);
}

void
read_release(struct rwspinlock *lk)
{
  releaseread(lk);
}

void
acquirewrite(struct rwspinlock *lk)
{
  acquire(&lk->lock);
  lk->pending_writers++;
  release(&lk->lock);

  rwlock_wait(lk, 1);
}

void
write_acquire(struct rwspinlock *lk)
{
  acquirewrite(lk);
}

void
releasewrite(struct rwspinlock *lk)
{
  acquire(&lk->lock);
  if(lk->writer == 0 || lk->cpu != mycpu())
    panic("releasewrite");
  lk->writer = 0;
  lk->cpu = 0;
  release(&lk->lock);
}

void
write_release(struct rwspinlock *lk)
{
  releasewrite(lk);
}

int
holdingwrite(struct rwspinlock *lk)
{
  int held;

  acquire(&lk->lock);
  held = lk->writer && lk->cpu == mycpu();
  release(&lk->lock);
  return held;
}

int
holding_write(struct rwspinlock *lk)
{
  return holdingwrite(lk);
}
