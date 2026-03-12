//
// test program for the alarm lab.
// you can modify this file for testing,
// but please make sure your kernel
// modifications pass the original
// versions of these tests.
//

#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/riscv.h"
#include "user/user.h"

void test0();
void test1();
void periodic();

static void __attribute__((noinline))
warmup(void)
{
}

static void
runtest(void (*fn)(void))
{
  int pid = fork();

  if(pid < 0){
    printf("alarmtest: fork failed\n");
    exit(1);
  }
  if(pid == 0){
    fn();
    exit(0);
  }
  wait(0);
}

int
main(void)
{
  warmup();
  runtest(test0);
  runtest(test1);
  printf("alarmtest: OK\n");
  exit(0);
}

volatile static int count;

void
periodic()
{
  count = count + 1;
  printf("alarm!\n");
  sigreturn();
}

// tests whether the kernel calls
// the alarm handler even a single time.
void
test0()
{
  int i;
  printf("test0 start\n");
  count = 0;
  sigalarm(2, periodic);
  for(i = 0; i < 10 && count == 0; i++)
    sleep(1);
  sigalarm(0, 0);
  if(count > 0){
    printf("test0 passed\n");
  } else {
    printf("test0 failed\n");
  }
}

void __attribute__ ((noinline)) foo(int i, int *j) {
  (void)i;
  *j += 1;
}

void
test1()
{
  int i;
  int j;

  printf("test1 start\n");
  count = 0;
  j = 0;
  sigalarm(2, periodic);
  for(i = 0; i < 5; i++){
    if(count >= 1)
      break;
    foo(i, &j);
    sleep(1);
  }
  if(i != j || count < 1){
    // i should equal j
    printf("test1 failed\n");
  } else {
    printf("test1 passed\n");
  }
  sigalarm(0, 0);
}
