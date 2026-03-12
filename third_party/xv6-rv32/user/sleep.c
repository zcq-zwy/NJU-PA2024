#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int ticks;

  if(argc != 2){
    fprintf(2, "usage: sleep ticks\n");
    exit(1);
  }

  ticks = atoi(argv[1]);
  if(ticks < 0){
    fprintf(2, "sleep: invalid ticks %s\n", argv[1]);
    exit(1);
  }

  // 直接把用户给出的 tick 数交给内核的 sleep 系统调用。
  sleep(ticks);
  exit(0);
}
