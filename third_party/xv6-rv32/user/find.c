#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fs.h"
#include "user/user.h"

static char *
basename(char *path)
{
  char *p;

  for(p = path + strlen(path); p >= path && *p != '/'; p--)
    ;
  return p + 1;
}

static void
find(char *path, char *target)
{
  char buf[512];
  char *p;
  int fd;
  struct dirent de;
  struct stat st;
  char name[DIRSIZ + 1];

  if((fd = open(path, 0)) < 0){
    fprintf(2, "find: cannot open %s\n", path);
    return;
  }

  if(fstat(fd, &st) < 0){
    fprintf(2, "find: cannot stat %s\n", path);
    close(fd);
    return;
  }

  // 先判断当前路径本身是否命中，便于支持从文件路径开始查找。
  if(strcmp(basename(path), target) == 0){
    printf("%s\n", path);
  }

  if(st.type != T_DIR){
    close(fd);
    return;
  }

  if(strlen(path) + 1 + DIRSIZ + 1 > sizeof(buf)){
    fprintf(2, "find: path too long\n");
    close(fd);
    return;
  }

  strcpy(buf, path);
  p = buf + strlen(buf);
  *p++ = '/';
  while(read(fd, &de, sizeof(de)) == sizeof(de)){
    if(de.inum == 0)
      continue;

    memmove(name, de.name, DIRSIZ);
    name[DIRSIZ] = 0;

    if(strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
      continue;

    memmove(p, de.name, DIRSIZ);
    p[DIRSIZ] = 0;
    find(buf, target);
  }

  close(fd);
}

int
main(int argc, char *argv[])
{
  if(argc != 3){
    fprintf(2, "usage: find path file\n");
    exit(1);
  }

  find(argv[1], argv[2]);
  exit(0);
}
