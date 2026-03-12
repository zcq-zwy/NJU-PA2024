#include "kernel/types.h"
#include "user/user.h"

int
main(void)
{
  int parent_to_child[2];
  int child_to_parent[2];
  char ch = 'x';
  int pid;

  if(pipe(parent_to_child) < 0 || pipe(child_to_parent) < 0){
    fprintf(2, "pingpong: pipe failed\n");
    exit(1);
  }

  pid = fork();
  if(pid < 0){
    fprintf(2, "pingpong: fork failed\n");
    exit(1);
  }

  if(pid == 0){
    // 子进程只负责从父进程收一个字节，再回一个字节。
    close(parent_to_child[1]);
    close(child_to_parent[0]);

    if(read(parent_to_child[0], &ch, 1) != 1){
      fprintf(2, "pingpong: child read failed\n");
      exit(1);
    }
    printf("%d: received ping\n", getpid());
    if(write(child_to_parent[1], &ch, 1) != 1){
      fprintf(2, "pingpong: child write failed\n");
      exit(1);
    }

    close(parent_to_child[0]);
    close(child_to_parent[1]);
    exit(0);
  }

  // 父进程先发，再等待子进程回传。
  close(parent_to_child[0]);
  close(child_to_parent[1]);

  if(write(parent_to_child[1], &ch, 1) != 1){
    fprintf(2, "pingpong: parent write failed\n");
    exit(1);
  }
  if(read(child_to_parent[0], &ch, 1) != 1){
    fprintf(2, "pingpong: parent read failed\n");
    exit(1);
  }
  printf("%d: received pong\n", getpid());

  close(parent_to_child[1]);
  close(child_to_parent[0]);
  wait(0);
  exit(0);
}
