// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[];  // first address after kernel.
                    // defined by kernel.ld.

struct run {
    struct run *next;
};

#define MAX_LOCKNAME_LEN 0x10

struct {
    struct spinlock lock;
    char lockname[MAX_LOCKNAME_LEN];  // lock name (required by kinit)
    struct run *freelist;
} kmem[NCPU];  // per-CPU freelists

void kinit() {
    // initlock(&kmem.lock, "kmem");
    for (int i = 0; i < sizeof(&kmem); i++) {
        snprintf(kmem[i].lockname, sizeof(kmem[i].lockname), "kmem%d", i + 1);  // kmem1, kmem2, kmem3, ...
        initlock(&kmem[i].lock, kmem[i].lockname);
    }
    freerange(end, (void *)PHYSTOP);
}

void freerange(void *pa_start, void *pa_end) {
    char *p;
    p = (char *)PGROUNDUP((uint64)pa_start);
    for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE)
        kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void kfree(void *pa) {
    struct run *r;

    if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
        panic("kfree");

    // Fill with junk to catch dangling refs.
    memset(pa, 1, PGSIZE);

    r = (struct run *)pa;

    int cid = cpuid();
    acquire(&kmem[cid].lock);
    r->next = kmem[cid].freelist;
    kmem[cid].freelist = r;
    release(&kmem[cid].lock);
}

// search and return a free memory from cpu[cid]
// if there is no free memory in cpu[cid], return 0
struct run *search_freemem(int cid) {
    struct run *mem = kmem[cid].freelist;
    if (mem) kmem[cid].freelist = mem->next;
    return mem;
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void) {
    struct run *r;

    int cid = cpuid();
    acquire(&kmem[cid].lock);
    /*
    r = kmem[cid].freelist;
    if (r)
        kmem[cid].freelist = r->next;
    */
    r = search_freemem(cid);
    if (!r) {
        // search free memory from other CPUs
        for (int i = 0; i < sizeof(&kmem); i++) {
            if (i == cid) continue;
            r = search_freemem(i);
            if (r) break;
        }
    }
    release(&kmem[cid].lock);

    if (r)
        memset((char *)r, 5, PGSIZE);  // fill with junk
    return (void *)r;
}
