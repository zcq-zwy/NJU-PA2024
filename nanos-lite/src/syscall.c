#include <common.h>
#include <fs.h>
#include "syscall.h"

static const char *syscall_name[] = {
  [SYS_exit] = "exit",
  [SYS_yield] = "yield",
  [SYS_open] = "open",
  [SYS_read] = "read",
  [SYS_write] = "write",
  [SYS_close] = "close",
  [SYS_lseek] = "lseek",
  [SYS_brk] = "brk",
  [SYS_gettimeofday] = "gettimeofday",
  [SYS_execve] = "execve",
};

static const char *get_syscall_name(uintptr_t id) {
  if (id < LENGTH(syscall_name) && syscall_name[id] != NULL) return syscall_name[id];
  return "unknown";
}

static bool should_trace_syscall(uintptr_t id, uintptr_t arg1, uintptr_t arg3) {
  if (id == SYS_write && (arg1 == 1 || arg1 == 2) && arg3 == 1) return false;
  return true;
}

void do_syscall(Context *c) {
  uintptr_t a[4];
  a[0] = c->GPR1;
  a[1] = c->GPR2;
  a[2] = c->GPR3;
  a[3] = c->GPR4;

  bool trace_this = should_trace_syscall(a[0], a[1], a[3]);
  if (trace_this) {
    Log("strace: syscall id=%d name=%s args=[%p, %p, %p]",
        a[0], get_syscall_name(a[0]), a[1], a[2], a[3]);
  }

  switch (a[0]) {
    case SYS_exit:
      Log("strace: syscall exit -> halt(%d)", a[1]);
      halt(a[1]);
      break;
    case SYS_yield:
      c->GPRx = 0;
      Log("strace: syscall yield -> ret=%d", c->GPRx);
      break;
    case SYS_write:
      c->GPRx = fs_write(a[1], (const void *)a[2], a[3]);
      if (trace_this) {
        Log("strace: syscall write -> ret=%d", c->GPRx);
      }
      break;
    case SYS_brk:
      c->GPRx = 0;
      Log("strace: syscall brk -> ret=%d", c->GPRx);
      break;
    default:
      panic("Unhandled syscall ID = %d", a[0]);
  }
}
