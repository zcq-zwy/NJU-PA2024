// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKET 13

struct bcache_bucket {
  struct spinlock lock;
  struct buf head;
};

struct {
  struct spinlock steal_lock;
  struct buf buf[NBUF];
  struct bcache_bucket bucket[NBUCKET];
} bcache;

static char *bucket_names[NBUCKET] = {
  "bcache0", "bcache1", "bcache2", "bcache3", "bcache4",
  "bcache5", "bcache6", "bcache7", "bcache8", "bcache9",
  "bcache10", "bcache11", "bcache12",
};

static int
bhash(uint dev, uint blockno)
{
  return (dev + blockno) % NBUCKET;
}

static void
bucket_remove(struct buf *b)
{
  b->next->prev = b->prev;
  b->prev->next = b->next;
}

static void
bucket_insert_head(struct buf *head, struct buf *b)
{
  b->next = head->next;
  b->prev = head;
  head->next->prev = b;
  head->next = b;
}

void
binit(void)
{
  struct buf *b;
  int i;

  initlock(&bcache.steal_lock, "bcache_steal");

  for(i = 0; i < NBUCKET; i++){
    initlock(&bcache.bucket[i].lock, bucket_names[i]);
    bcache.bucket[i].head.prev = &bcache.bucket[i].head;
    bcache.bucket[i].head.next = &bcache.bucket[i].head;
  }

  for(i = 0, b = bcache.buf; b < bcache.buf + NBUF; b++, i = (i + 1) % NBUCKET){
    initsleeplock(&b->lock, "buffer");
    b->bucketno = i;
    b->dev = 0;
    b->blockno = 0;
    b->valid = 0;
    b->refcnt = 0;
    bucket_insert_head(&bcache.bucket[i].head, b);
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  struct buf *freebuf;
  int home, i;

  home = bhash(dev, blockno);
  acquire(&bcache.bucket[home].lock);

  for(b = bcache.bucket[home].head.next; b != &bcache.bucket[home].head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.bucket[home].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  for(b = bcache.bucket[home].head.prev; b != &bcache.bucket[home].head; b = b->prev){
    if(b->refcnt == 0){
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.bucket[home].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  release(&bcache.bucket[home].lock);
  acquire(&bcache.steal_lock);
  acquire(&bcache.bucket[home].lock);

  for(b = bcache.bucket[home].head.next; b != &bcache.bucket[home].head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.bucket[home].lock);
      release(&bcache.steal_lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  for(b = bcache.bucket[home].head.prev; b != &bcache.bucket[home].head; b = b->prev){
    if(b->refcnt == 0){
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.bucket[home].lock);
      release(&bcache.steal_lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  freebuf = 0;
  for(i = 0; i < NBUCKET; i++){
    if(i == home)
      continue;
    acquire(&bcache.bucket[i].lock);
    for(b = bcache.bucket[i].head.prev; b != &bcache.bucket[i].head; b = b->prev){
      if(b->refcnt == 0){
        bucket_remove(b);
        freebuf = b;
        break;
      }
    }
    release(&bcache.bucket[i].lock);
    if(freebuf)
      break;
  }

  if(freebuf == 0){
    release(&bcache.bucket[home].lock);
    release(&bcache.steal_lock);
    panic("bget: no buffers");
  }

  freebuf->bucketno = home;
  freebuf->dev = dev;
  freebuf->blockno = blockno;
  freebuf->valid = 0;
  freebuf->refcnt = 1;
  bucket_insert_head(&bcache.bucket[home].head, freebuf);

  release(&bcache.bucket[home].lock);
  release(&bcache.steal_lock);
  acquiresleep(&freebuf->lock);
  return freebuf;
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the MRU list.
void
brelse(struct buf *b)
{
  int bucketno;

  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  bucketno = b->bucketno;
  acquire(&bcache.bucket[bucketno].lock);
  b->refcnt--;
  if(b->refcnt == 0){
    bucket_remove(b);
    bucket_insert_head(&bcache.bucket[bucketno].head, b);
  }
  release(&bcache.bucket[bucketno].lock);
}

void
bpin(struct buf *b) {
  acquire(&bcache.bucket[b->bucketno].lock);
  b->refcnt++;
  release(&bcache.bucket[b->bucketno].lock);
}

void
bunpin(struct buf *b) {
  acquire(&bcache.bucket[b->bucketno].lock);
  b->refcnt--;
  release(&bcache.bucket[b->bucketno].lock);
}

