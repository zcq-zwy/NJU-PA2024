#include "kernel/types.h"
#include "user/user.h"

#define SZ 4096

char buf[SZ];

int
main(void)
{
  int i, n;

  for(;;){
    n = statistics(buf, SZ);
    for(i = 0; i < n; i++)
      write(1, buf + i, 1);
    if(n != SZ)
      break;
  }
  exit(0);
}
