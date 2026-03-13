#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "sysinfo.h"

uint32
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint32
sys_getpid(void)
{
  return myproc()->pid;
}

uint32
sys_fork(void)
{
  return fork();
}

uint32
sys_wait(void)
{
  uint32 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint32
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint32
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint32
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint32
sys_uptime(void)
{
  uint xticks;
  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint32
sys_pause(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  if(n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < (uint)n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint32
sys_cpupin(void)
{
  struct proc *p = myproc();
  int cpu;

  if(argint(0, &cpu) < 0)
    return -1;
  if(cpu < 0 || cpu >= NCPU)
    return -1;

  acquire(&p->lock);
  p->pincpu = &cpus[cpu];
  release(&p->lock);
  return 0;
}

uint32
sys_trace(void)
{
  int mask;

  if(argint(0, &mask) < 0)
    return -1;
  myproc()->tracemask = mask;
  return 0;
}

uint32
sys_sysinfo(void)
{
  uint32 addr;
  struct sysinfo info;

  if(argaddr(0, &addr) < 0)
    return -1;

  info.freemem = kfreemem();
  info.nproc = proc_count();

  if(copyout(myproc()->pagetable, addr, (char *)&info, sizeof(info)) < 0)
    return -1;
  return 0;
}

uint32
sys_sigalarm(void)
{
  int interval;
  uint32 handler;
  struct proc *p = myproc();

  if(argint(0, &interval) < 0)
    return -1;
  if(argaddr(1, &handler) < 0)
    return -1;

  p->alarm_interval = interval;
  p->alarm_elapsed = 0;
  p->alarm_handler = handler;
  p->alarm_active = 0;
  acquire(&tickslock);
  p->alarm_last_tick = ticks;
  release(&tickslock);
  memset(&p->alarm_tf, 0, sizeof(p->alarm_tf));
  return 0;
}

uint32
sys_sigreturn(void)
{
  struct proc *p = myproc();
  uint32 a0;

  if(!p->alarm_active)
    return -1;

  a0 = p->alarm_tf.a0;
  *(p->tf) = p->alarm_tf;
  p->alarm_active = 0;
  acquire(&tickslock);
  p->alarm_last_tick = ticks;
  release(&tickslock);
  return a0;
}
