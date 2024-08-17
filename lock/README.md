# [MIT 6.S081 Fall 2020] Lab: locks

> https://pdos.csail.mit.edu/6.S081/2020/labs/lock.html

## Memory allocator

`kmem`을 CPU의 개수(`NCPU`)만큼의 element들을 갖는 배열로 수정한다.

```c
/* kernel/kalloc.c */

struct {
    struct spinlock lock;
    struct run *freelist;
} kmem[NCPU];
```

CPU마다 lock name을 다르게 만들어야 하므로, `kmem` 구조체에 `lockname`을 추가한다.

```c
/* kernel/kalloc.c */

#define LOCKNAME_MAX 0x10
struct {
    struct spinlock lock;
    char lockname[LOCKNAME_MAX];
    struct run *freelist;
} kmem[NCPU];
```

`kinit()`에서 `kmem`의 모든 element들을 초기화하도록 수정한다.

```c
/* kernel/kalloc.c */

void kinit() {
    for (int i = 0; i < NCPU; i++) {
        snprintf(kmem[i].lockname, LOCKNAME_MAX, "kmem%d", i + 1); // kmem1, kmem2, kmem3, ...
        initlock(&kmem[i].lock, kmem[i].lockname);
    }
    freerange(end, (void *)PHYSTOP);
}
```

`kfree()`에서 free한 메모리가 해당하는 CPU의 `freelist`에 들어가도록 수정한다.

```c
/* kernel/kalloc.c */

void kfree(void *pa) {
...
    int cid = cpuid();
    acquire(&kmem[cid].lock);
    r->next = kmem[cid].freelist;
    kmem[cid].freelist = r;
    release(&kmem[cid].lock);
}
```

`kalloc()`에서 현재 CPU의 `freelist`가 비어 있으면 다른 CPU의 `freelist`에서 free memory를 가져오도록 수정한다.

```c
/* kernel/kalloc.c */

void *
kalloc(void) {
...
    if (r)
        kmem[cid].freelist = r->next;
    else {
        for (int i = 0; i < NCPU; i++) {
            r = kmem[i].freelist;
            if (r) {
                kmem[i].freelist = r->next;
                break;
            }
        }
    }
...
}
```

![image](https://github.com/h0meb0dy/h0meb0dy/assets/104156058/f44f404a-24e8-40de-af53-cf3b95f405ad)

## Buffer cache

`bcache`를 bcache bucket의 개수만큼 element들을 갖는 배열로 수정한다.

```c
/* kernel/bio.c */

#define NBUCKET 5
#define LOCKNAME_MAX 0x10
struct {
    struct spinlock lock;
    char lockname[LOCKNAME_MAX + 1];
    struct buf buf[NBUF];

    // Linked list of all buffers, through prev/next.
    // Sorted by how recently the buffer was used.
    // head.next is most recent, head.prev is least.
    struct buf head;
} bcache[NBUCKET];
```

`binit()`에서 `bcache`의 모든 element들을 초기화하도록 수정한다.

```c
/* kernel/bio.c */

void binit(void) {
    struct buf *b;

    for (int i = 0; i < NBUCKET; i++) {
        snprintf(bcache[i].lockname, LOCKNAME_MAX, "bcache%d", i + 1); // bcache1, bcache2, bcache3, ...
        initlock(&bcache[i].lock, bcache[i].lockname);

        // Create linked list of buffers
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
```

Buffer들을 여러 개의 bcache에 분산시키기 위해 `dev`와 `blockno`에 따라 bcache index를 계산하는 `bcache_idx()` 함수를 정의한다.

```c
/* kernel/bio.c */

int bcache_idx(uint dev, uint blockno) {
    return (((uint64)dev << 32) | blockno) % NBUCKET;
}
```

`bget()`에서 bcache index를 계산하여 그 index에 해당하는 bcache를 get하도록 수정한다.

```c
/* kernel/bio.c */

static struct buf *
bget(uint dev, uint blockno) {
    struct buf *b;

    int idx = bcache_idx(dev, blockno);

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
```

`brelse`에서 bcache index를 계산하여 그 index에 해당하는 bcache를 release하도록 수정한다.

```c
/* kernel/bio.c */

void brelse(struct buf *b) {
    if (!holdingsleep(&b->lock))
        panic("brelse");

    int idx = bcache_idx(b->dev, b->blockno);

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
```

`bpin()`과 `bunpin()`에서도 마찬가지로 bcache index를 계산하여 pin과 unpin하도록 수정한다.

```c
/* kernel/bio.c */

void bpin(struct buf *b) {
    int idx = bcache_idx(b->dev, b->blockno);
    acquire(&bcache[idx].lock);
    b->refcnt++;
    release(&bcache[idx].lock);
}

void bunpin(struct buf *b) {
    int idx = bcache_idx(b->dev, b->blockno);
    acquire(&bcache[idx].lock);
    b->refcnt--;
    release(&bcache[idx].lock);
}
```

![image](https://github.com/h0meb0dy/h0meb0dy/assets/104156058/7b6d94f4-c6f4-452a-a7e4-089df14014e5)
