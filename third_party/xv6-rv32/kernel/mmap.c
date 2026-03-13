#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "proc.h"
#include "defs.h"
#include "fs.h"
#include "file.h"
#include "fcntl.h"

static int
argfd_local(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;
  struct proc *p = myproc();

  if(argint(n, &fd) < 0)
    return -1;
  if(fd < 0 || fd >= NOFILE)
    return -1;
  f = p->ofile[fd];
  if(f == 0)
    return -1;
  if(pfd)
    *pfd = fd;
  if(pf)
    *pf = f;
  return 0;
}

static struct vma*
vma_find(struct proc *p, uint32 va)
{
  int i;

  for(i = 0; i < NVMA; i++){
    struct vma *v = &p->vmas[i];
    if(v->used && va >= v->addr && va < v->addr + v->len)
      return v;
  }
  return 0;
}

static struct vma*
vma_alloc(struct proc *p)
{
  int i;

  for(i = 0; i < NVMA; i++){
    if(!p->vmas[i].used)
      return &p->vmas[i];
  }
  return 0;
}

static uint32
vma_alloc_addr(struct proc *p, uint32 len)
{
  uint32 top;
  int i;

  top = TRAPFRAME;
  for(i = 0; i < NVMA; i++){
    if(p->vmas[i].used && p->vmas[i].addr < top)
      top = p->vmas[i].addr;
  }

  if(top < len)
    return 0;
  top = PGROUNDDOWN(top - len);
  if(top < PGROUNDUP(p->sz))
    return 0;
  return top;
}

static int
vma_perm(struct vma *v)
{
  int perm;

  perm = PTE_U;
  if(v->prot & PROT_READ)
    perm |= PTE_R;
  if(v->prot & PROT_WRITE)
    perm |= PTE_R | PTE_W;
  if(v->prot & PROT_EXEC)
    perm |= PTE_X;
  return perm;
}

static int
vma_writeback(struct proc *p, struct vma *v, uint32 addr, uint32 len)
{
  uint32 a;
  int wrote_any;

  if((v->flags & MAP_SHARED) == 0 || (v->prot & PROT_WRITE) == 0)
    return 0;

  wrote_any = 0;
  begin_op();
  ilock(v->file->ip);
  for(a = addr; a < addr + len; a += PGSIZE){
    uint32 pa, fileoff;
    int n;

    pa = walkaddr(p->pagetable, a);
    if(pa == 0)
      continue;

    fileoff = v->offset + (a - v->addr);
    if(fileoff >= v->file->ip->size)
      continue;

    n = PGSIZE;
    if(fileoff + n > v->file->ip->size)
      n = v->file->ip->size - fileoff;
    if(writei(v->file->ip, 0, pa, fileoff, n) != n){
      iunlock(v->file->ip);
      end_op();
      return -1;
    }
    wrote_any = 1;
  }
  iunlock(v->file->ip);
  end_op();

  return wrote_any >= 0 ? 0 : -1;
}

static int
vma_unmap(struct proc *p, struct vma *v, uint32 addr, uint32 len)
{
  uint32 a;

  if(addr < v->addr || addr + len > v->addr + v->len)
    return -1;
  if(addr != v->addr && addr + len != v->addr + v->len)
    return -1;

  if(vma_writeback(p, v, addr, len) < 0)
    return -1;

  for(a = addr; a < addr + len; a += PGSIZE){
    if(walkaddr(p->pagetable, a) != 0)
      uvmunmap(p->pagetable, a, PGSIZE, 1);
  }

  if(addr == v->addr && len == v->len){
    fileclose(v->file);
    memset(v, 0, sizeof(*v));
  } else if(addr == v->addr){
    v->addr += len;
    v->offset += len;
    v->len -= len;
  } else {
    v->len -= len;
  }

  return 0;
}

uint32
sys_mmap(void)
{
  uint32 addr, len, offset;
  int prot, flags;
  struct file *f;
  struct vma *v;
  struct proc *p;

  if(argaddr(0, &addr) < 0 ||
     argaddr(1, &len) < 0 ||
     argint(2, &prot) < 0 ||
     argint(3, &flags) < 0 ||
     argfd_local(4, 0, &f) < 0 ||
     argaddr(5, &offset) < 0)
    return -1;

  if(addr != 0 || len == 0 || offset % PGSIZE != 0)
    return -1;
  if(flags != MAP_SHARED && flags != MAP_PRIVATE)
    return -1;
  if((prot & (PROT_READ | PROT_WRITE | PROT_EXEC)) == 0)
    return -1;
  if((prot & PROT_READ) && !f->readable)
    return -1;
  if((flags & MAP_SHARED) && (prot & PROT_WRITE) && !f->writable)
    return -1;

  p = myproc();
  len = PGROUNDUP(len);
  v = vma_alloc(p);
  if(v == 0)
    return -1;

  addr = vma_alloc_addr(p, len);
  if(addr == 0)
    return -1;

  filedup(f);
  v->used = 1;
  v->addr = addr;
  v->len = len;
  v->prot = prot;
  v->flags = flags;
  v->offset = offset;
  v->file = f;

  return addr;
}

uint32
sys_munmap(void)
{
  uint32 addr, len;
  struct proc *p;
  struct vma *v;

  if(argaddr(0, &addr) < 0 || argaddr(1, &len) < 0)
    return -1;
  if(len == 0 || addr % PGSIZE != 0 || len % PGSIZE != 0)
    return -1;

  p = myproc();
  v = vma_find(p, addr);
  if(v == 0)
    return -1;
  if(vma_unmap(p, v, addr, len) < 0)
    return -1;
  return 0;
}

int
mmap_handle_pagefault(struct proc *p, uint32 va, uint32 scause)
{
  struct vma *v;
  uint32 va0, fileoff;
  char *mem;
  int n;

  va0 = PGROUNDDOWN(va);
  v = vma_find(p, va);
  if(v == 0)
    return -1;

  if(scause == 13 && (v->prot & PROT_READ) == 0)
    return -1;
  if(scause == 15 && (v->prot & PROT_WRITE) == 0)
    return -1;
  if(scause == 12 && (v->prot & PROT_EXEC) == 0)
    return -1;
  if(walkaddr(p->pagetable, va0) != 0)
    return -1;

  mem = kalloc();
  if(mem == 0)
    return -1;
  memset(mem, 0, PGSIZE);

  fileoff = v->offset + (va0 - v->addr);
  ilock(v->file->ip);
  n = readi(v->file->ip, 0, (uint32)mem, fileoff, PGSIZE);
  iunlock(v->file->ip);
  if(n < 0){
    kfree(mem);
    return -1;
  }

  if(mappages(p->pagetable, va0, PGSIZE, (uint32)mem, vma_perm(v)) < 0){
    kfree(mem);
    return -1;
  }

  return 0;
}

void
mmap_fork(struct proc *p, struct proc *np)
{
  int i;

  for(i = 0; i < NVMA; i++){
    if(!p->vmas[i].used)
      continue;
    np->vmas[i] = p->vmas[i];
    filedup(np->vmas[i].file);
  }
}

void
mmap_proc_cleanup(struct proc *p)
{
  int i;

  for(i = 0; i < NVMA; i++){
    if(!p->vmas[i].used)
      continue;
    if(vma_unmap(p, &p->vmas[i], p->vmas[i].addr, p->vmas[i].len) < 0){
      fileclose(p->vmas[i].file);
      memset(&p->vmas[i], 0, sizeof(p->vmas[i]));
    }
  }
}
