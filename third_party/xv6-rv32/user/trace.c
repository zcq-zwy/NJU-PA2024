#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int mask;
  int i;
  char *nargv[32];

  if(argc < 3){
    fprintf(2, "usage: trace mask command\n");
    exit(1);
  }

  mask = atoi(argv[1]);
  if(trace(mask) < 0){
    fprintf(2, "trace: trace syscall failed\n");
    exit(1);
  }

  for(i = 2; i < argc && i - 2 < 31; i++){
    nargv[i - 2] = argv[i];
  }
  nargv[i - 2] = 0;

  exec(nargv[0], nargv);
  fprintf(2, "trace: exec %s failed\n", nargv[0]);
  exit(1);
}
