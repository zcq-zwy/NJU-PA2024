#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define NREADERS 3
#define NREADS 10

static void
reader(void)
{
  int i;

  for(i = 0; i < NREADS; i++)
    uptime();
  exit(0);
}

static int
run_readers(void)
{
  int i, pid, status;
  int start, end;

  start = uptime();
  for(i = 0; i < NREADERS; i++){
    pid = fork();
    if(pid < 0)
      return -1;
    if(pid == 0)
      reader();
  }

  for(i = 0; i < NREADERS; i++){
    if(wait(&status) < 0 || status != 0)
      return -1;
  }

  end = uptime();
  return end - start;
}

static int
run_writer(void)
{
  int start, end;

  start = uptime();
  if(pause(10) < 0)
    return -1;
  end = uptime();
  return end - start;
}

int
main(void)
{
  int read_ticks, write_ticks;

  read_ticks = run_readers();
  if(read_ticks < 0){
    printf("rwlktest: reader FAIL\n");
    exit(1);
  }

  if(read_ticks > 16){
    printf("rwlktest: reader too slow (%d ticks)\n", read_ticks);
    exit(1);
  }
  printf("rwlktest: readers OK (%d ticks)\n", read_ticks);

  write_ticks = run_writer();
  if(write_ticks < 0){
    printf("rwlktest: writer FAIL\n");
    exit(1);
  }

  if(write_ticks < 10 || write_ticks > 16){
    printf("rwlktest: writer bad timing (%d ticks)\n", write_ticks);
    exit(1);
  }
  printf("rwlktest: writer OK (%d ticks)\n", write_ticks);

  printf("rwlktest: OK\n");
  exit(0);
}
