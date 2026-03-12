#include "kernel/types.h"
#include "kernel/riscv.h"
#include "kernel/sysinfo.h"
#include "user/user.h"

static void
fail(char *msg)
{
  printf("sysinfotest: %s failed\n", msg);
  exit(1);
}

static void
testmem(void)
{
  struct sysinfo info;

  if(sysinfo(&info) < 0)
    fail("sysinfo");

  if(sbrk(PGSIZE) == (char *)-1)
    fail("sbrk");

  if(sysinfo(&info) < 0)
    fail("sysinfo");
}

static void
testproc(void)
{
  struct sysinfo info;
  int pid;

  if(sysinfo(&info) < 0)
    fail("sysinfo");

  pid = fork();
  if(pid < 0)
    fail("fork");
  if(pid == 0){
    sleep(5);
    exit(0);
  }

  if(sysinfo(&info) < 0)
    fail("sysinfo");
  wait(0);
}

int
main(void)
{
  testmem();
  testproc();
  printf("sysinfotest: OK\n");
  exit(0);
}
