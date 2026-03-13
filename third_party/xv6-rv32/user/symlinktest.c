#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "kernel/stat.h"
#include "user/user.h"

static void
fail(const char *msg)
{
  fprintf(2, "symlinktest: %s\n", msg);
  exit(1);
}

static void
test_basic(void)
{
  int fd;
  char buf[16];

  unlink("target");
  unlink("link");

  fd = open("target", O_CREATE | O_RDWR);
  if(fd < 0)
    fail("cannot create target");
  if(write(fd, "hello", 5) != 5)
    fail("cannot write target");
  close(fd);

  if(symlink("target", "link") < 0)
    fail("cannot create link");

  fd = open("link", O_RDONLY);
  if(fd < 0)
    fail("cannot open link");
  memset(buf, 0, sizeof(buf));
  if(read(fd, buf, sizeof(buf)) != 5)
    fail("cannot read link");
  close(fd);
  if(strcmp(buf, "hello") != 0)
    fail("basic content mismatch");

  printf("symlinktest: basic OK\n");
}

static void
test_chain(void)
{
  int fd;
  char buf[16];

  unlink("link2");
  if(symlink("link", "link2") < 0)
    fail("cannot create second link");

  fd = open("link2", O_RDONLY);
  if(fd < 0)
    fail("cannot open second link");
  memset(buf, 0, sizeof(buf));
  if(read(fd, buf, sizeof(buf)) != 5)
    fail("cannot read second link");
  close(fd);
  if(strcmp(buf, "hello") != 0)
    fail("chain content mismatch");

  printf("symlinktest: chain OK\n");
}

static void
test_nofollow(void)
{
  int fd;
  struct stat st;

  fd = open("link", O_RDONLY | O_NOFOLLOW);
  if(fd < 0)
    fail("cannot open with O_NOFOLLOW");
  if(fstat(fd, &st) < 0)
    fail("fstat failed");
  close(fd);
  if(st.type != T_SYMLINK)
    fail("O_NOFOLLOW did not open symlink inode");

  printf("symlinktest: nofollow OK\n");
}

static void
test_cycle(void)
{
  int fd;

  unlink("loop1");
  unlink("loop2");
  if(symlink("loop2", "loop1") < 0)
    fail("cannot create loop1");
  if(symlink("loop1", "loop2") < 0)
    fail("cannot create loop2");

  fd = open("loop1", O_RDONLY);
  if(fd >= 0){
    close(fd);
    fail("cycle should fail");
  }

  printf("symlinktest: cycle OK\n");
}

int
main(void)
{
  test_basic();
  test_chain();
  test_nofollow();
  test_cycle();

  unlink("loop2");
  unlink("loop1");
  unlink("link2");
  unlink("link");
  unlink("target");

  printf("symlinktest: OK\n");
  exit(0);
}
