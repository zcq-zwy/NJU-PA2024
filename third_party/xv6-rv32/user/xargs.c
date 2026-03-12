#include "kernel/types.h"
#include "kernel/param.h"
#include "user/user.h"

#define XARGS_BUFSZ  512

static void
run_line(char *base_argv[], int base_argc, char *line)
{
  char *argv[MAXARG];
  int i;
  int pid;
  char *p;

  for(i = 0; i < base_argc; i++){
    argv[i] = base_argv[i];
  }

  p = line;
  while(*p != 0){
    // 跳过每一行里的空白，把剩余内容按参数切分。
    while(*p == ' ' || *p == '\t')
      p++;
    if(*p == 0)
      break;

    if(i + 1 >= MAXARG){
      fprintf(2, "xargs: too many arguments\n");
      exit(1);
    }

    argv[i++] = p;
    while(*p != 0 && *p != ' ' && *p != '\t')
      p++;
    if(*p == 0)
      break;
    *p++ = 0;
  }
  argv[i] = 0;

  if(i == base_argc)
    return;

  pid = fork();
  if(pid < 0){
    fprintf(2, "xargs: fork failed\n");
    exit(1);
  }

  if(pid == 0){
    exec(argv[0], argv);
    fprintf(2, "xargs: exec %s failed\n", argv[0]);
    exit(1);
  }

  wait(0);
}

int
main(int argc, char *argv[])
{
  char buf[XARGS_BUFSZ];
  int n;
  int pos;
  char ch;

  if(argc < 2){
    fprintf(2, "usage: xargs command [args ...]\n");
    exit(1);
  }

  pos = 0;
  while((n = read(0, &ch, 1)) == 1){
    if(ch == '\n'){
      buf[pos] = 0;
      run_line(&argv[1], argc - 1, buf);
      pos = 0;
      continue;
    }

    if(pos + 1 >= sizeof(buf)){
      fprintf(2, "xargs: input line too long\n");
      exit(1);
    }
    buf[pos++] = ch;
  }

  if(n < 0){
    fprintf(2, "xargs: read failed\n");
    exit(1);
  }

  if(pos > 0){
    buf[pos] = 0;
    run_line(&argv[1], argc - 1, buf);
  }

  exit(0);
}
