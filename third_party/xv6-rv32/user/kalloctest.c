#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define NCHILD 3
#define NPAGES 128

static void
alloc_worker(void)
{
  char *pages[NPAGES];
  int i, j;

  for(i = 0; i < NPAGES; i++){
    pages[i] = sbrk(4096);
    if(pages[i] == (char*)-1)
      exit(1);
    for(j = 0; j < 4096; j += 512)
      pages[i][j] = i + j;
  }

  for(i = 0; i < NPAGES; i++){
    for(j = 0; j < 4096; j += 512){
      if(pages[i][j] != (char)(i + j))
        exit(1);
    }
  }

  exit(0);
}

int
main(void)
{
  int i, status, pid;

  for(i = 0; i < NCHILD; i++){
    pid = fork();
    if(pid < 0){
      printf("kalloctest: fork failed\n");
      exit(1);
    }
    if(pid == 0)
      alloc_worker();
  }

  for(i = 0; i < NCHILD; i++){
    if(wait(&status) < 0 || status != 0){
      printf("kalloctest: FAIL\n");
      exit(1);
    }
  }

  printf("kalloctest: OK\n");
  exit(0);
}
