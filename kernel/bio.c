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
#define HASH_BUCKET_NUM 17

struct hash_bucket {
  struct spinlock bucket_lock;
  struct buf link_head;
};
static struct hash_bucket block_hash[HASH_BUCKET_NUM];
struct {
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
} bcache;

void
binit(void)
{
  struct buf *b;
  for(int idx = 0; idx < HASH_BUCKET_NUM; idx++){
    initlock(&block_hash[idx].bucket_lock, "bcache_bucket");
    block_hash[idx].link_head.next = 0;
  }
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    initsleeplock(&b->lock, "buffer");
    b->blockno = 0;
    b->tickstamp = 0;
    b->next = block_hash[0].link_head.next;
    block_hash[0].link_head.next = b;
  }
}

static struct buf*
recycle_buf(struct buf *victim_buf, uint dev, uint blockno)
{
  int target_bucket = blockno % HASH_BUCKET_NUM;
  int old_bucket = victim_buf->blockno % HASH_BUCKET_NUM;

  victim_buf->dev = dev;
  victim_buf->blockno = blockno;
  victim_buf->valid = 0;
  victim_buf->refcnt = 1;
  victim_buf->tickstamp = ticks;

  // 如果新旧哈希桶不一致，从旧桶摘除节点
  if(old_bucket != target_bucket){
    struct buf *cur = &block_hash[old_bucket].link_head;
    while(cur->next != victim_buf){
      cur = cur->next;
    }
    cur->next = victim_buf->next;
    release(&block_hash[old_bucket].bucket_lock);
    // 插入目标桶链表头部
    victim_buf->next = block_hash[target_bucket].link_head.next;
    block_hash[target_bucket].link_head.next = victim_buf;
  }
  release(&block_hash[target_bucket].bucket_lock);
  acquiresleep(&victim_buf->lock);
  return victim_buf;
}

struct buf*
bget(uint dev, uint blockno)
{
  struct buf *p;
  int target_bucket = blockno % HASH_BUCKET_NUM;

  acquire(&block_hash[target_bucket].bucket_lock);
  // 在当前哈希桶查找是否已经缓存该磁盘块
  for(p = block_hash[target_bucket].link_head.next; p != 0; p = p->next){
    if(p->dev == dev && p->blockno == blockno){
      p->refcnt += 1;
      release(&block_hash[target_bucket].bucket_lock);
      acquiresleep(&p->lock);
      return p;
    }
  }
  // 缓存未命中，遍历全部哈希桶寻找LRU可回收缓冲区
  int scan_idx = target_bucket;
  do{
    if(scan_idx != target_bucket){
      acquire(&block_hash[scan_idx].bucket_lock);
    }
    struct buf *lru_candidate = 0;
    uint min_tick = 0xFFFFFFFFU;
    for(p = block_hash[scan_idx].link_head.next; p != 0; p = p->next){
      if(p->refcnt == 0 && p->tickstamp < min_tick){
        min_tick = p->tickstamp;
        lru_candidate = p;
      }
    }
    if(lru_candidate != 0){
      return recycle_buf(lru_candidate, dev, blockno);
    }
    if(scan_idx != target_bucket){
      release(&block_hash[scan_idx].bucket_lock);
    }
    scan_idx = (scan_idx + 1) % HASH_BUCKET_NUM;
  }while(scan_idx != target_bucket);

  panic("bget: no available buffer");
}
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");
  releasesleep(&b->lock);

  int bkt = b->blockno % HASH_BUCKET_NUM;
  acquire(&block_hash[bkt].bucket_lock);
  b->refcnt--;
  if(b->refcnt == 0){
    b->tickstamp = ticks;
  }
  release(&block_hash[bkt].bucket_lock);
}

void
bpin(struct buf *b)
{
  int bkt = b->blockno % HASH_BUCKET_NUM;
  acquire(&block_hash[bkt].bucket_lock);
  b->refcnt++;
  release(&block_hash[bkt].bucket_lock);
}

void
bunpin(struct buf *b)
{
  int bkt = b->blockno % HASH_BUCKET_NUM;
  acquire(&block_hash[bkt].bucket_lock);
  b->refcnt--;
  release(&block_hash[bkt].bucket_lock);
}

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

void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}
