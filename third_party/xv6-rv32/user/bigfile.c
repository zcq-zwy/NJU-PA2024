#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "kernel/fs.h"
#include "user/user.h"

#define QUICK_BLOCKS (NDIRECT + NINDIRECT + 32)
#define MAXFILE_BLOCKS (NDIRECT + NINDIRECT + NDINDIRECT)
#define CHUNK_BLOCKS 16

static char buf[1024 * CHUNK_BLOCKS];
static char path[] = "bigfile.data";

static void
fill_chunk(int start_block, int blocks)
{
  int i;
  memset(buf, 0, sizeof(buf));
  for(i = 0; i < blocks; i++){
    ((int *)(buf + i * 1024))[0] = start_block + i;
  }
}

static void
check_chunk(int start_block, int blocks)
{
  int i;
  for(i = 0; i < blocks; i++){
    if(((int *)(buf + i * 1024))[0] != start_block + i){
      fprintf(2, "bigfile: wrong data at block %d\n", start_block + i);
      exit(1);
    }
  }
}

int
main(int argc, char **argv)
{
  int fd, done, blocks, nbytes;
  int total_blocks = QUICK_BLOCKS;

  if(argc >= 2){
    total_blocks = atoi(argv[1]);
    if(total_blocks <= 0 || total_blocks > MAXFILE_BLOCKS){
      fprintf(2, "usage: bigfile [1..%d]\n", MAXFILE_BLOCKS);
      exit(1);
    }
  }

  fd = open(path, O_CREATE | O_RDWR);
  if(fd < 0){
    fprintf(2, "bigfile: cannot create bigfile\n");
    exit(1);
  }

  for(done = 0; done < total_blocks; done += blocks){
    blocks = total_blocks - done;
    if(blocks > CHUNK_BLOCKS)
      blocks = CHUNK_BLOCKS;
    fill_chunk(done, blocks);
    nbytes = blocks * 1024;
    if(write(fd, buf, nbytes) != nbytes){
      fprintf(2, "bigfile: short write before reaching maxfile\n");
      close(fd);
      exit(1);
    }
  }

  if(total_blocks == MAXFILE_BLOCKS){
    fill_chunk(MAXFILE_BLOCKS, 1);
    if(write(fd, buf, 1024) == 1024){
      fprintf(2, "bigfile: write beyond maxfile succeeded\n");
      close(fd);
      exit(1);
    }
  }
  close(fd);

  fd = open(path, O_RDONLY);
  if(fd < 0){
    fprintf(2, "bigfile: cannot open bigfile\n");
    exit(1);
  }

  for(done = 0; done < total_blocks; done += blocks){
    blocks = total_blocks - done;
    if(blocks > CHUNK_BLOCKS)
      blocks = CHUNK_BLOCKS;
    nbytes = blocks * 1024;
    if(read(fd, buf, nbytes) != nbytes){
      fprintf(2, "bigfile: short read before end of file\n");
      close(fd);
      exit(1);
    }
    check_chunk(done, blocks);
  }

  if(read(fd, buf, 1024) != 0){
    fprintf(2, "bigfile: read past end returned data\n");
    close(fd);
    exit(1);
  }
  close(fd);

  if(total_blocks < MAXFILE_BLOCKS)
    unlink(path);

  printf("bigfile: OK\n");
  exit(0);
}
