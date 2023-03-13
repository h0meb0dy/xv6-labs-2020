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

#define NBUCKET 7              // number of bcache buckets
#define MAX_LOCKNAME_LEN 0x10  // maximum length of lock name

struct {
    struct spinlock lock;
    char lockname[MAX_LOCKNAME_LEN + 1];  // lock name (required by binit)
    struct buf buf[NBUF];

    // Linked list of all buffers, through prev/next.
    // Sorted by how recently the buffer was used.
    // head.next is most recent, head.prev is least.
    struct buf head;
} bcache[NBUCKET];  // distributed buffer caches

void binit(void) {
    struct buf *b;

    // initlock(&bcache.lock, "bcache");
    for (int i = 0; i < sizeof(bcache) / sizeof(bcache[0]); i++) {
        snprintf(bcache[i].lockname, sizeof(bcache[i].lockname) - 1, "bcache%d", i + 1);  // bcache1, bcache2, bcache3, ...
        initlock(&bcache[i].lock, bcache[i].lockname);
    }

    // Create linked list of buffers
    for (int i = 0; i < sizeof(bcache) / sizeof(bcache[0]); i++) {
        bcache[i].head.prev = &bcache[i].head;
        bcache[i].head.next = &bcache[i].head;
        for (b = bcache[i].buf; b < bcache[i].buf + NBUF; b++) {
            b->next = bcache[i].head.next;
            b->prev = &bcache[i].head;
            initsleeplock(&b->lock, "buffer");
            bcache[i].head.next->prev = b;
            bcache[i].head.next = b;
        }
    }
}

// calculate bcache index with dev and blockno
int bcache_idx(uint dev, uint blockno) {
    return (((uint64)dev << 32) | blockno) % (sizeof(bcache) / sizeof(bcache[0]));
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf *
bget(uint dev, uint blockno) {
    struct buf *b;

    int idx = bcache_idx(dev, blockno);  // bcache index

    acquire(&bcache[idx].lock);

    // Is the block already cached?
    for (b = bcache[idx].head.next; b != &bcache[idx].head; b = b->next) {
        if (b->dev == dev && b->blockno == blockno) {
            b->refcnt++;
            release(&bcache[idx].lock);
            acquiresleep(&b->lock);
            return b;
        }
    }

    // Not cached.
    // Recycle the least recently used (LRU) unused buffer.
    for (b = bcache[idx].head.prev; b != &bcache[idx].head; b = b->prev) {
        if (b->refcnt == 0) {
            b->dev = dev;
            b->blockno = blockno;
            b->valid = 0;
            b->refcnt = 1;
            release(&bcache[idx].lock);
            acquiresleep(&b->lock);
            return b;
        }
    }
    panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf *
bread(uint dev, uint blockno) {
    struct buf *b;

    b = bget(dev, blockno);
    if (!b->valid) {
        virtio_disk_rw(b, 0);
        b->valid = 1;
    }
    return b;
}

// Write b's contents to disk.  Must be locked.
void bwrite(struct buf *b) {
    if (!holdingsleep(&b->lock))
        panic("bwrite");
    virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void brelse(struct buf *b) {
    if (!holdingsleep(&b->lock))
        panic("brelse");

    int idx = bcache_idx(b->dev, b->blockno);  // bcache index

    releasesleep(&b->lock);

    acquire(&bcache[idx].lock);
    b->refcnt--;
    if (b->refcnt == 0) {
        // no one is waiting for it.
        b->next->prev = b->prev;
        b->prev->next = b->next;
        b->next = bcache[idx].head.next;
        b->prev = &bcache[idx].head;
        bcache[idx].head.next->prev = b;
        bcache[idx].head.next = b;
    }

    release(&bcache[idx].lock);
}

void bpin(struct buf *b) {
    int idx = bcache_idx(b->dev, b->blockno);  // bcache index

    acquire(&bcache[idx].lock);
    b->refcnt++;
    release(&bcache[idx].lock);
}

void bunpin(struct buf *b) {
    int idx = bcache_idx(b->dev, b->blockno);  // bcache index

    acquire(&bcache[idx].lock);
    b->refcnt--;
    release(&bcache[idx].lock);
}
