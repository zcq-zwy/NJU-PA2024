#include "kernel/types.h"
#include "user/user.h"

static int __attribute__((noinline))
g(int x)
{
  return x + 3;
}

static int __attribute__((noinline))
f(int x)
{
  return g(x);
}

int
main(void)
{
  printf("%d %d\n", f(8) + 1, 13);
  exit(0);
}
