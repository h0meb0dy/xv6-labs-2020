# [MIT 6.S081 Fall 2020] Lab: mmap

> https://pdos.csail.mit.edu/6.S081/2020/labs/mmap.html

`Makefile`에 `mmaptest`를 추가합니다.

```makefile
# Makefile

UPROGS=\
...
	$U/_mmaptest\
```

`mmap`과 `munmap` system call에 필요한 선언과 정의들을 추가합니다.

```c
/* kernel/syscall.h */

// System call numbers
...
#define SYS_mmap 22
#define SYS_munmap 23
```

```c
/* kernel/syscall.c */

...
extern uint64 sys_mmap(void);
extern uint64 sys_munmap(void);

static uint64 (*syscalls[])(void) = {
...
    [SYS_mmap] sys_mmap,
    [SYS_munmap] sys_munmap,
};
```

```c
/* user/user.h */

// system calls
...
void *mmap(void *, int, int, int, int, int);
int munmap(void *, int);
```

```perl
# user/usys.pl

entry("fork");
...
entry("mmap");
entry("munmap");
```

`struct vma`를 정의하고, `struct proc`에 VMA list를 추가합니다.

```c
/* kernel/proc.h */

struct vma {
    void *addr;     // address
    int length;     // length
    int prot;       // permission
    int flag;       // flag
    struct file *f; // file
    int used;       // 1 if used
};

// Per-process state
struct proc {
...
    struct vma vma[16]; // VMA list
};
```

`sys_mmap()`을 구현합니다.

```c
/* kernel/sysfile.c */

uint64 sys_mmap(void) {
    void *addr;
    int length, prot, flags, fd, offset;

    /* fetch arguments */
    if (argaddr(0, (void *)&addr) || argint(1, &length) || argint(2, &prot) || argint(3, &flags) || argint(4, &fd) || argint(5, &offset)) {
        return -1;
    }
    if (addr || offset || length <= 0) {
        return -1;
    }

    /* find unused VMA */
    struct proc *p = myproc();
    struct vma *v = 0;
    for (int i = 0; i < sizeof(p->vma) / sizeof(p->vma[0]); i++) {
        if (!p->vma[i].used) {
            v = &p->vma[i];
            break;
        }
    }
    if (!v) {
        return -1; // if VMA list is full, mmap fails
    }

    /* allocate VMA (lazily) */
    v->addr = (void *)p->sz;
    v->length = length;
    p->sz += PGROUNDUP(length);
    v->prot = prot;
    v->flag = flags;
    v->f = filedup(p->ofile[fd]);
    v->used = 1;

    return (uint64)v->addr;
}
```

`usertrap()`에서 `sys_mmap()`의 lazy allocation으로 인해 발생하는 fault를 처리합니다.

```c
/* kernel/trap.c */

...
#include "sleeplock.h"
#include "fs.h"
#include "file.h"

...
void usertrap(void) {
...
    } else if ((which_dev = devintr()) != 0) {
        // ok
    } else if (r_scause() == 13 || r_scause() == 15) {
        struct vma *v = 0;
        void *va = 0;

        /* check if fault is caused by lazy allocation in sys_mmap() */
        for (int i = 0; i < sizeof(p->vma) / sizeof(p->vma[0]); i++) {
            void *fault_addr = (void *)r_stval(); // faulted address
            if (p->vma[i].used && p->vma[i].addr <= fault_addr && fault_addr < p->vma[i].addr + p->vma[i].length) {
                v = &p->vma[i];
                va = (void *)PGROUNDDOWN((uint64)fault_addr);
                break;
            }
        }
        if (!va) {
            exit(-1);
        }

        /* allocate VMA (really) */
        char *mem = kalloc();
        if (!mem) {
            exit(-1);
        }
        memset(mem, 0, PGSIZE);
        if (mappages(p->pagetable, (uint64)va, PGSIZE, (uint64)mem, PTE_W | PTE_X | PTE_R | PTE_U) != 0) {
            kfree(mem);
            exit(-1);
        }

        /* copy a page from file */
        readi(v->f->ip, 0, (uint64)mem, va - v->addr, PGSIZE);
    } else {
...
}
```

다음으로 `sys_munmap()`을 구현합니다.

```c
/* kernel/sysfile.c */

uint64 sys_munmap(void) {
    void *addr;
    int length;

    /* fetch arguments */
    if (argaddr(0, (void *)&addr) || argint(1, &length)) {
        return -1;
    }

    struct proc *p = myproc();
    struct vma *v = 0;
    void *va = 0;

    /* find corresponded VMA */
    for (int i = 0; i < sizeof(p->vma) / sizeof(p->vma[0]); i++) {
        if (p->vma[i].used && p->vma[i].addr <= addr && addr < p->vma[i].addr + p->vma[i].length) {
            v = &p->vma[i];
            va = (void *)PGROUNDDOWN((uint64)addr);
            break;
        }
    }
    if (!v) {
        return -1;
    }
    length = PGROUNDUP((uint64)addr + length) - (uint64)va; // alignment

    if (va != v->addr && va + length != v->addr + v->length) {
        return -1; // munmap cannot punch a hole in VMA
    }

    /* deallocate VMA */
    for (addr = va; addr < va + length; addr += PGSIZE) {
        uvmunmap(p->pagetable, (uint64)addr, 1, 1);
        v->length -= PGSIZE;
    }
    if (va == v->addr) {
        // if unmap from the start, modify address of VMA
        v->addr += length;
    }
    if (!v->length) {
        // if whole VMA is deallocated, decrease reference count of file and mark as not-used
        fileclose(v->f);
        v->used = 0;
    }

    return 0;
}
```

`uvmunmap()`에서 실제로 map되지 않은 (lazy allocation) page를 unmap하려고 시도할 때 발생하는 panic을 제거합니다.

```c
/* kernel/vm.c */

void uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free) {
...
    for (a = va; a < va + npages * PGSIZE; a += PGSIZE) {
...
        if ((*pte & PTE_V) == 0)
            // panic("uvmunmap: not mapped");
            continue;
...
    }
}
```

`MAP_SHARED` flag가 활성화되어 있을 경우, 수정 사항이 원본 파일에 반영되어야 합니다. 따라서 `sys_munmap()`에서 `uvmunmap()`을 호출하기 전에 page의 내용을 파일에 쓰도록 수정합니다.

```c
/* kernel/sysfile.c */

uint64 sys_munmap(void) {
...
    /* deallocate VMA */
    for (addr = va; addr < va + length; addr += PGSIZE) {
        if (v->flag & MAP_SHARED) {
            // if MAP_SHARED flag is set, reflect changes to file
            filewrite(v->f, (uint64)addr, PGSIZE);
        }
        uvmunmap(p->pagetable, (uint64)addr, 1, 1);
        v->length -= PGSIZE;
    }
...
}
```

`mmaptest`에서 read-only로 열린 파일에 대하여 `PROT_WRITE` 권한과 `MAP_SHARED` 플래그를 설정하는 경우를 테스트합니다.

```c
/* user/mmaptest.c */

void
mmap_test(void)
{
...
  p = mmap(0, PGSIZE*3, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (p != MAP_FAILED)
    err("mmap call should have failed");
...
}
```

이 경우에는 수정 사항이 원본 파일에 반영될 수 없으므로 `mmap`이 실패하도록 수정합니다.

```c
/* kernel/sysfile.c */

uint64 sys_mmap(void) {
...
    struct proc *p = myproc();
    struct file *f = filedup(p->ofile[fd]);

    /* if file is read-only and WRITE permission is set and MAP_SHARED flag is set, mmap fails */
    if (!f->writable && prot & PROT_WRITE && flags & MAP_SHARED) {
        fileclose(f);
        return -1;
    }

    /* find unused VMA */
    struct vma *v = 0;
...
}
```

Lazy allocation으로 인해 `uvmcopy()`에서 발생하는 panic을 제거합니다.

```c
/* kernel/vm.c */

int uvmcopy(pagetable_t old, pagetable_t new, uint64 sz) {
...
    for (i = 0; i < sz; i += PGSIZE) {
,,,
        if ((*pte & PTE_V) == 0)
            // panic("uvmcopy: page not present");
            continue;
,,,
    }
...
}
```

`fork()`에 VMA list를 복사하는 코드를 추가합니다.

```c
/* kernel/proc.c */

int fork(void) {
...
    /* copy VMA list */
    for (int i = 0; i < sizeof(p->vma) / sizeof(p->vma[0]); i++) {
        np->vma[i] = p->vma[i];
    }

    release(&np->lock);

    return pid;
}
```

![image](https://github.com/h0meb0dy/h0meb0dy/assets/104156058/3846ce69-634d-43fe-bfab-55373eab41a0)

![image](https://github.com/h0meb0dy/h0meb0dy/assets/104156058/3c80f08a-85d0-4a81-bef4-a114d79a064d)
