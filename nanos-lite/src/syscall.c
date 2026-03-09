#include <common.h>
#include <fs.h>
#include <proc.h>
#include <sys/time.h>

#include "syscall.h"

#ifdef CONFIG_STRACE
#define STRACE_LOG(...) Log(__VA_ARGS__)

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
  if (id == SYS_gettimeofday) return false;
  if (id == SYS_read && strcmp(fs_get_filename(arg1), "/dev/events") == 0) return false;
  if (id == SYS_write && strcmp(fs_get_filename(arg1), "/dev/fb") == 0) return false;
  if (id == SYS_write && (arg1 == 1 || arg1 == 2) && arg3 == 1) return false;
  return true;
}

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR   2
#define O_ACCMODE (O_RDONLY | O_WRONLY | O_RDWR)
#define O_APPEND 0x0008
#define O_CREAT  0x0200
#define O_TRUNC  0x0400
#define O_EXCL   0x0800

static const char *get_whence_name(uintptr_t whence) {
  switch (whence) {
    case SEEK_SET: return "SEEK_SET";
    case SEEK_CUR: return "SEEK_CUR";
    case SEEK_END: return "SEEK_END";
    default: return "SEEK_UNKNOWN";
  }
}

static const char *get_accmode_name(uintptr_t flags) {
  switch (flags & O_ACCMODE) {
    case O_RDONLY: return "O_RDONLY";
    case O_WRONLY: return "O_WRONLY";
    case O_RDWR: return "O_RDWR";
    default: return "O_?";
  }
}

static void format_open_flags(uintptr_t flags, char *buf, size_t size) {
  int n = snprintf(buf, size, "%s", get_accmode_name(flags));
  if (n < 0 || (size_t)n >= size) return;
  if (flags & O_CREAT)  n += snprintf(buf + n, size - n, "|O_CREAT");
  if ((size_t)n >= size) return;
  if (flags & O_TRUNC)  n += snprintf(buf + n, size - n, "|O_TRUNC");
  if ((size_t)n >= size) return;
  if (flags & O_APPEND) n += snprintf(buf + n, size - n, "|O_APPEND");
  if ((size_t)n >= size) return;
  if (flags & O_EXCL)   n += snprintf(buf + n, size - n, "|O_EXCL");
}
#else
#define STRACE_LOG(...)
static inline bool should_trace_syscall(uintptr_t id, uintptr_t arg1, uintptr_t arg3) {
  (void)id;
  (void)arg1;
  (void)arg3;
  return false;
}
static inline const char *get_syscall_name(uintptr_t id) {
  (void)id;
  return "unknown";
}
static inline const char *get_whence_name(uintptr_t whence) {
  (void)whence;
  return "SEEK_UNKNOWN";
}
static inline void format_open_flags(uintptr_t flags, char *buf, size_t size) {
  (void)flags;
  if (size > 0) buf[0] = 0;
}
#endif

static void sys_execve(const char *filename, char *const argv[], char *const envp[]) {
  context_uload(current, filename, argv, envp);
  switch_boot_pcb();
  yield();
  panic("SYS_execve should not return");
}

void do_syscall(Context *c) {
  uintptr_t a[4];
  a[0] = c->GPR1;
  a[1] = c->GPR2;
  a[2] = c->GPR3;
  a[3] = c->GPR4;

  bool trace_this = should_trace_syscall(a[0], a[1], a[3]);
  if (trace_this) {
    if (a[0] == SYS_open) {
      char flagbuf[64];
      format_open_flags(a[2], flagbuf, sizeof(flagbuf));
      STRACE_LOG("strace: syscall id=%d name=%s path=\"%s\" flags=%s mode=%p",
          a[0], get_syscall_name(a[0]), (const char *)a[1], flagbuf, a[3]);
    } else if (a[0] == SYS_lseek) {
      STRACE_LOG("strace: syscall id=%d name=%s fd=%d(%s) offset=%p whence=%s",
          a[0], get_syscall_name(a[0]), a[1], fs_get_filename(a[1]), a[2], get_whence_name(a[3]));
    } else if (a[0] == SYS_read || a[0] == SYS_write || a[0] == SYS_close) {
      STRACE_LOG("strace: syscall id=%d name=%s fd=%d(%s) args=[%p, %p, %p]",
          a[0], get_syscall_name(a[0]), a[1], fs_get_filename(a[1]), a[1], a[2], a[3]);
    } else {
      STRACE_LOG("strace: syscall id=%d name=%s args=[%p, %p, %p]",
          a[0], get_syscall_name(a[0]), a[1], a[2], a[3]);
    }
  }

  switch (a[0]) {
    case SYS_exit: {
      static char *nterm_argv[] = { "/bin/nterm", NULL };
      STRACE_LOG("strace: syscall exit(%d) -> execve(\"/bin/nterm\")", a[1]);
      sys_execve("/bin/nterm", nterm_argv, NULL);
      break;
    }
    case SYS_yield:
      c->GPRx = 0;
      STRACE_LOG("strace: syscall yield -> ret=%d", c->GPRx);
      break;
    case SYS_open:
      c->GPRx = fs_open((const char *)a[1], a[2], a[3]);
      if (trace_this) {
        STRACE_LOG("strace: syscall open -> ret=%d fd=%d(%s)", c->GPRx, c->GPRx, fs_get_filename(c->GPRx));
      }
      break;
    case SYS_read:
      c->GPRx = fs_read(a[1], (void *)a[2], a[3]);
      if (trace_this) {
        STRACE_LOG("strace: syscall read -> ret=%d", c->GPRx);
      }
      break;
    case SYS_write:
      c->GPRx = fs_write(a[1], (const void *)a[2], a[3]);
      if (trace_this) {
        STRACE_LOG("strace: syscall write -> ret=%d", c->GPRx);
      }
      break;
    case SYS_close:
      c->GPRx = fs_close(a[1]);
      if (trace_this) {
        STRACE_LOG("strace: syscall close -> ret=%d", c->GPRx);
      }
      break;
    case SYS_lseek:
      c->GPRx = fs_lseek(a[1], a[2], a[3]);
      if (trace_this) {
        STRACE_LOG("strace: syscall lseek -> ret=%d", c->GPRx);
      }
      break;
    case SYS_brk:
      c->GPRx = 0;
      STRACE_LOG("strace: syscall brk -> ret=%d", c->GPRx);
      break;
    case SYS_execve:
      STRACE_LOG("strace: syscall execve -> filename=\"%s\"", (const char *)a[1]);
      sys_execve((const char *)a[1], (char *const *)a[2], (char *const *)a[3]);
      break;
    case SYS_gettimeofday: {
      struct timeval *tv = (struct timeval *)a[1];
      AM_TIMER_UPTIME_T uptime = io_read(AM_TIMER_UPTIME);
      if (tv != NULL) {
        tv->tv_sec = uptime.us / 1000000;
        tv->tv_usec = uptime.us % 1000000;
      }
      c->GPRx = 0;
      if (trace_this) {
        STRACE_LOG("strace: syscall gettimeofday -> ret=%d tv=[%ld,%ld]",
            c->GPRx, tv ? (long)tv->tv_sec : -1L, tv ? (long)tv->tv_usec : -1L);
      }
      break;
    }
    default:
      panic("Unhandled syscall ID = %d", a[0]);
  }
}
