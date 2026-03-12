#include "kernel/types.h"
#include "kernel/riscv.h"
#include "kernel/sysinfo.h"
#include "user/user.h"

#define MIN_TEST_BYTES (64 * 1024)
#define MAX_TEST_BYTES (128 * 1024)

static void
fail(char *msg)
{
  printf("cowtest: %s failed\n", msg);
  exit(1);
}

static uint
round_down_page(uint value)
{
  return value & ~(PGSIZE - 1);
}

static uint
testsize(void)
{
  struct sysinfo info;
  uint sz;

  if(sysinfo(&info) < 0)
    fail("sysinfo");

  sz = info.freemem / 8;
  if(sz < MIN_TEST_BYTES)
    sz = MIN_TEST_BYTES;
  if(sz > MAX_TEST_BYTES)
    sz = MAX_TEST_BYTES;
  sz = round_down_page(sz);
  return sz;
}

static uint
midoff(uint sz)
{
  return round_down_page(sz / 2);
}

static uint
tailoff(uint sz)
{
  return sz - PGSIZE;
}

static void
set_marks(char *mem, uint sz, char base)
{
  mem[0] = base;
  mem[midoff(sz)] = base + 1;
  mem[tailoff(sz)] = base + 2;
}

static void
check_marks(char *mem, uint sz, char base, char *who)
{
  if(mem[0] != base || mem[midoff(sz)] != base + 1 || mem[tailoff(sz)] != base + 2)
    fail(who);
}

static void
simpletest(void)
{
  char *mem;
  struct sysinfo info;
  uint32 stats[2];
  uint parent_free, fork_drop, write_drop;
  uint sz;
  int fd[2];
  int pid;

  printf("simple: ");
  sz = testsize();
  mem = sbrk(sz);
  if(mem == (char *)-1)
    fail("sbrk");

  set_marks(mem, sz, 1);

  if(pipe(fd) < 0)
    fail("pipe simple");
  if(sysinfo(&info) < 0)
    fail("sysinfo parent");
  parent_free = info.freemem;

  pid = fork();
  if(pid < 0)
    fail("fork");
  if(pid == 0){
    close(fd[0]);
    if(sysinfo(&info) < 0)
      fail("sysinfo child before");
    stats[0] = info.freemem;
    set_marks(mem, sz, 4);
    check_marks(mem, sz, 4, "child simple");
    if(sysinfo(&info) < 0)
      fail("sysinfo child after");
    stats[1] = info.freemem;
    if(write(fd[1], stats, sizeof(stats)) != sizeof(stats))
      fail("write simple");
    close(fd[1]);
    exit(0);
  }

  close(fd[1]);
  if(read(fd[0], stats, sizeof(stats)) != sizeof(stats))
    fail("read simple");
  close(fd[0]);
  if(wait(0) != pid)
    fail("wait simple");
  check_marks(mem, sz, 1, "parent simple");
  fork_drop = parent_free - stats[0];
  write_drop = stats[0] - stats[1];
  if(fork_drop > sz / 4)
    fail("fork copied too much memory");
  if(write_drop < 3 * PGSIZE || write_drop > 32 * PGSIZE)
    fail("write did not trigger cow");

  if(sbrk(-((int)sz)) == (char *)-1)
    fail("sbrk shrink");
  printf("ok\n");
}

static void
threetest(void)
{
  char *mem;
  uint sz;
  int pid1, pid2;

  printf("three: ");
  sz = testsize() / 16;
  sz = round_down_page(sz);
  if(sz < 8 * PGSIZE)
    sz = 8 * PGSIZE;

  mem = sbrk(sz);
  if(mem == (char *)-1)
    fail("sbrk three");
  set_marks(mem, sz, 10);

  pid1 = fork();
  if(pid1 < 0)
    fail("fork one");
  if(pid1 == 0){
    pid2 = fork();
    if(pid2 < 0)
      fail("fork two");
    if(pid2 == 0){
      set_marks(mem, sz, 20);
      check_marks(mem, sz, 20, "grandchild three");
      exit(0);
    }
    if(wait(0) != pid2)
      fail("wait two");
    check_marks(mem, sz, 10, "child three");
    exit(0);
  }

  if(wait(0) != pid1)
    fail("wait one");
  check_marks(mem, sz, 10, "parent three");
  if(sbrk(-((int)sz)) == (char *)-1)
    fail("sbrk shrink three");
  printf("ok\n");
}

static void
forkforktest(void)
{
  char *mem;
  uint sz;
  int pid, status;

  printf("forkfork: ");
  sz = 3 * PGSIZE;
  mem = sbrk(sz);
  if(mem == (char *)-1)
    fail("sbrk forkfork");
  set_marks(mem, sz, 30);

  pid = fork();
  if(pid < 0)
    fail("fork outer");
  if(pid == 0){
    int inner = fork();
    if(inner < 0)
      fail("fork inner");
    if(inner == 0){
      set_marks(mem, sz, 40);
      exit(0);
    }
    if(wait(&status) != inner || status != 0)
      fail("wait inner");
    check_marks(mem, sz, 30, "child forkfork");
    exit(0);
  }

  if(wait(&status) != pid || status != 0)
    fail("wait outer");
  check_marks(mem, sz, 30, "parent forkfork");
  if(sbrk(-((int)sz)) == (char *)-1)
    fail("sbrk shrink forkfork");
  printf("ok\n");
}

static void
filetest(void)
{
  int fd[2];
  int pid;
  char *buf;

  printf("file: ");
  buf = sbrk(PGSIZE);
  if(buf == (char *)-1)
    fail("sbrk file");
  buf[0] = 'a';

  if(pipe(fd) < 0)
    fail("pipe");
  pid = fork();
  if(pid < 0)
    fail("fork file");
  if(pid == 0){
    close(fd[0]);
    if(write(fd[1], "x", 1) != 1)
      fail("write");
    close(fd[1]);
    exit(0);
  }

  close(fd[1]);
  if(read(fd[0], buf, 1) != 1)
    fail("read");
  close(fd[0]);
  if(wait(0) != pid)
    fail("wait file");
  if(buf[0] != 'x')
    fail("copyout");
  if(sbrk(-PGSIZE) == (char *)-1)
    fail("sbrk shrink file");
  printf("ok\n");
}

int
main(void)
{
  simpletest();
  simpletest();
  threetest();
  threetest();
  threetest();
  forkforktest();
  filetest();
  printf("cowtest: OK\n");
  exit(0);
}
