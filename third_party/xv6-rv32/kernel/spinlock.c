// Mutual exclusion spin locks.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "proc.h"
#include "defs.h"

#define NLOCK 512

static struct spinlock *locks[NLOCK];
static struct spinlock lock_locks;
static int lock_locks_bootstrapped;

static int rwlktest_ready;
static int rwlktest_initing;
static struct rwspinlock rwlktest_lock;

static void
bootstrap_lock_registry(void)
{
  if(lock_locks_bootstrapped)
    return;
  lock_locks.name = "lock_locks";
  lock_locks.locked = 0;
  lock_locks.cpu = 0;
  lock_locks.nts = 0;
  lock_locks.n = 0;
  lock_locks_bootstrapped = 1;
}

static void
register_lock(struct spinlock *lk)
{
  int i, free_slot;

  if(lk == &lock_locks)
    return;

  bootstrap_lock_registry();

  acquire(&lock_locks);
  free_slot = -1;
  for(i = 0; i < NLOCK; i++){
    if(locks[i] == lk){
      release(&lock_locks);
      return;
    }
    if(locks[i] == 0 && free_slot < 0)
      free_slot = i;
  }
  if(free_slot < 0){
    release(&lock_locks);
    panic("register_lock");
  }
  locks[free_slot] = lk;
  release(&lock_locks);
}

static int
append_char(char *buf, int sz, int off, char c)
{
  if(off + 1 < sz)
    buf[off] = c;
  return off + 1;
}

static int
append_str(char *buf, int sz, int off, char *s)
{
  if(s == 0)
    s = "(null)";
  while(*s)
    off = append_char(buf, sz, off, *s++);
  return off;
}

static int
append_uint(char *buf, int sz, int off, uint x)
{
  char tmp[16];
  int i = 0;

  do {
    tmp[i++] = '0' + x % 10;
    x /= 10;
  } while(x != 0);
  while(--i >= 0)
    off = append_char(buf, sz, off, tmp[i]);
  return off;
}

static int
finish_buf(char *buf, int sz, int off)
{
  if(sz > 0){
    if(off >= sz)
      buf[sz - 1] = 0;
    else
      buf[off] = 0;
  }
  return off;
}

static int
snprint_lock(char *buf, int sz, struct spinlock *lk)
{
  int off = 0;

  if(lk == 0 || lk->name == 0 || lk->n <= 0)
    return 0;

  off = append_str(buf, sz, off, "lock: ");
  off = append_str(buf, sz, off, lk->name);
  off = append_str(buf, sz, off, ": #test-and-set ");
  off = append_uint(buf, sz, off, lk->nts);
  off = append_str(buf, sz, off, " #acquire() ");
  off = append_uint(buf, sz, off, lk->n);
  off = append_char(buf, sz, off, '\n');
  return finish_buf(buf, sz, off);
}

void
freelock(struct spinlock *lk)
{
  int i;

  if(!lock_locks_bootstrapped)
    return;

  acquire(&lock_locks);
  for(i = 0; i < NLOCK; i++){
    if(locks[i] == lk){
      locks[i] = 0;
      break;
    }
  }
  release(&lock_locks);
}

void
initlock(struct spinlock *lk, char *name)
{
  lk->name = name;
  lk->locked = 0;
  lk->cpu = 0;
  lk->nts = 0;
  lk->n = 0;
  register_lock(lk);
}

void
acquire(struct spinlock *lk)
{
  int nts = 0;

  push_off();
  if(holding(lk))
    panic("acquire");

  while(__sync_lock_test_and_set(&lk->locked, 1) != 0)
    nts++;

  __sync_synchronize();
  lk->cpu = mycpu();
  lk->n++;
  lk->nts += nts;
}

void
release(struct spinlock *lk)
{
  if(!holding(lk))
    panic("release");

  lk->cpu = 0;
  __sync_synchronize();
  __sync_lock_release(&lk->locked);
  pop_off();
}

int
holding(struct spinlock *lk)
{
  return lk->locked && lk->cpu == mycpu();
}

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
  if(c->noff < 1)
    panic("pop_off");
  c->noff -= 1;
  if(c->noff == 0 && c->intena)
    intr_on();
}

int
atomic_read4(int *addr)
{
  __sync_synchronize();
  return *addr;
}

int
statslock(char *buf, int sz)
{
  int i, j, off, total;
  int top[5];
  char *dst;
  int room;

  if(sz <= 0)
    return 0;

  off = 0;
  total = 0;
  for(i = 0; i < 5; i++)
    top[i] = -1;

  acquire(&lock_locks);

  off = append_str(buf, sz, off, "--- lock kmem/bcache stats\n");
  for(i = 0; i < NLOCK; i++){
    if(locks[i] == 0 || locks[i]->name == 0)
      continue;
    if(strncmp(locks[i]->name, "kmem", 4) == 0 ||
       strncmp(locks[i]->name, "bcache", 6) == 0){
      total += locks[i]->nts;
      dst = buf + (off < sz ? off : sz - 1);
      room = sz - off;
      if(room < 0)
        room = 0;
      off += snprint_lock(dst, room, locks[i]);
    }
  }

  off = append_str(buf, sz, off, "--- top 5 contended locks:\n");
  for(i = 0; i < 5; i++){
    int best = -1;
    for(j = 0; j < NLOCK; j++){
      int used = 0;
      if(locks[j] == 0 || locks[j]->name == 0 || locks[j]->n <= 0)
        continue;
      for(int k = 0; k < i; k++){
        if(top[k] == j){
          used = 1;
          break;
        }
      }
      if(used)
        continue;
      if(best < 0 || locks[j]->nts > locks[best]->nts)
        best = j;
    }
    if(best < 0)
      break;
    top[i] = best;
    dst = buf + (off < sz ? off : sz - 1);
    room = sz - off;
    if(room < 0)
      room = 0;
    off += snprint_lock(dst, room, locks[best]);
  }

  off = append_str(buf, sz, off, "tot= ");
  off = append_uint(buf, sz, off, total);
  off = append_char(buf, sz, off, '\n');

  release(&lock_locks);
  return finish_buf(buf, sz, off);
}

void
initrwlock(struct rwspinlock *lk, char *name)
{
  initlock(&lk->lock, name);
  lk->readers = 0;
  lk->writer = 0;
  lk->waiting_writers = 0;
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
  push_off();
  for(;;){
    acquire(&lk->lock);
    if(lk->writer == 0 && lk->waiting_writers == 0){
      lk->readers++;
      release(&lk->lock);
      return;
    }
    release(&lk->lock);
  }
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
  pop_off();
}

void
read_release(struct rwspinlock *lk)
{
  releaseread(lk);
}

void
acquirewrite(struct rwspinlock *lk)
{
  push_off();
  acquire(&lk->lock);
  lk->waiting_writers++;
  while(lk->writer || lk->readers){
    release(&lk->lock);
    acquire(&lk->lock);
  }
  lk->waiting_writers--;
  lk->writer = 1;
  lk->cpu = mycpu();
  release(&lk->lock);
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
  pop_off();
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

uint32
sys_rwlktest(void)
{
  int i;

  for(;;){
    acquire(&lock_locks);
    if(rwlktest_ready){
      release(&lock_locks);
      break;
    }
    if(rwlktest_initing == 0){
      rwlktest_initing = 1;
      release(&lock_locks);
      initrwlock(&rwlktest_lock, "rwlktest");
      acquire(&lock_locks);
      rwlktest_ready = 1;
      rwlktest_initing = 0;
      release(&lock_locks);
      break;
    }
    release(&lock_locks);
  }

  for(i = 0; i < 2000; i++){
    read_acquire(&rwlktest_lock);
    read_release(&rwlktest_lock);
  }
  for(i = 0; i < 1000; i++){
    write_acquire(&rwlktest_lock);
    write_release(&rwlktest_lock);
  }
  return 0;
}
