# [MIT 6.S081 Fall 2020] Lab: xv6 lazy page allocation

> https://pdos.csail.mit.edu/6.S081/2020/labs/lazy.html

Lazy allocation은 메모리 효율성을 위한 메모리 관리 기법이다. 메모리 할당 요청이 들어왔을 때는 할당할 메모리의 주소만 반환하고, 그 주소에 처음으로 접근할 떄 실제로 메모리를 할당한다. 실제로 할당되지 않은 메모리에 접근을 시도하면 trap이 발생하는데, trap handler에서 메모리를 할당해주는 식으로 처리한다. 이 lazy allocation을 구현하는 것이 실습의 목표이다.

## Eliminate allocation from sbrk()

`sys_sbrk()` 시스템 콜에서는 `growproc()`을 호출하여 프로세스에 메모리를 할당한다. `growproc()`은 할당할 메모리의 크기만큼 프로세스의 메모리 크기(`p->sz`)를 증가시키고, `uvmalloc()`으로 user virtual memory를 할당한다. 이 부분을 실제 메모리 할당 없이 프로세스의 메모리 크기만 증가시키도록 수정한다.

```c
/* kernel/sysproc.c */

uint64
sys_sbrk(void) {
    int addr;
    int n;
    struct proc *p = myproc();

    if (argint(0, &n) < 0)
        return -1;
    // addr = myproc()->sz;
    // if (growproc(n) < 0)
    //     return -1;
    addr = p->sz; // old size
    p->sz += n;   // increment process size
    return addr;
}
```

![image](https://github.com/h0meb0dy/h0meb0dy/assets/104156058/ce34977b-ddca-4415-a6c3-3ebec98805cf)

## Lazy allocation

기본적인 lazy allocation을 구현한다. 접근할 수 없는 page에 접근을 시도하면 page fault가 발생하는데, 이 경우 `scause`는 13 또는 15이다. `usertrap()`에서 `scause`가 13이나 15인 경우에 lazy allocation을 하도록 구현한다.

```c
/* kernel/trap.c */

void usertrap(void) {
...
    if (r_scause() == 8) {
...
    } else if ((which_dev = devintr()) != 0) {
        // ok
    } else if (r_scause() == 13 || r_scause() == 15) {
        uint64 va = PGROUNDDOWN(r_stval()); // address of faulted page
        char *mem = kalloc();
        if (!mem) {
            p->killed = 1;
        } else {
            memset(mem, 0, PGSIZE);
            if (mappages(p->pagetable, va, PGSIZE, (uint64)mem, PTE_W | PTE_X | PTE_R | PTE_U)) {
                kfree(mem);
                p->killed = 1;
            }
        }
    } else {
...
    }
...
}
```

Mapping되지 않은 page를 unmap하려고 시도하면 `uvmunmap()`에서 `uvmunmap: not mapped` panic이 발생한다. 그런데 한 번도 접근하지 않은 page는 실제로 할당되어 있지 않기 때문에 mapping되지 않은 것은 정상적인 상황이다. 따라서 panic을 발생시키는 코드를 제거한다.

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

![image](https://github.com/h0meb0dy/h0meb0dy/assets/104156058/8e39e134-ce85-4082-bff0-8cfdfe2f853e)

## Lazytests and Usertests

`sbrk()`의 인자(`n`)가 음수인 경우, 프로세스의 사이즈(`p->sz`)를 `n`만큼 감소시키고 그 페이지들을 unmap한다.

```c
/* kernel/sysproc.c */

uint64
sys_sbrk(void) {
...
    addr = p->sz; // old size
    p->sz += n;   // increment (or decrement) process size
    if (n < 0) {
        uvmunmap(p->pagetable, p->sz, (addr - p->sz) / PGSIZE, 1);
    }
    return addr;
}
```

`uvmunmap()`의 `walk()`에서 발생하는 panic을 제거한다.

```js
/* kernel/vm.c */

void uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free) {
...
    for (a = va; a < va + npages * PGSIZE; a += PGSIZE) {
        if ((pte = walk(pagetable, a, 0)) == 0)
            // panic("uvmunmap: walk");
            continue;
...
    }
}
```

Page fault가 발생한 주소가 `p->sz`보다 크면 비정상적인 상황이므로 프로세스를 kill한다.

```c
/* kernel/trap.c */

void usertrap(void) {
...
    if (r_scause() == 8) {
...
    } else if ((which_dev = devintr()) != 0) {
        // ok
    } else if (r_scause() == 13 || r_scause() == 15) {
        uint64 va = PGROUNDDOWN(r_stval()); // address of faulted page
        if (va > p->sz) {
            p->killed = 1;
        } else {
            char *mem = kalloc();
            if (!mem) {
                p->killed = 1;
            } else {
                memset(mem, 0, PGSIZE);
                if (mappages(p->pagetable, va, PGSIZE, (uint64)mem, PTE_W | PTE_X | PTE_R | PTE_U)) {
                    kfree(mem);
                    p->killed = 1;
                }
            }
        }
    } else {
...
    }
...
}
```

`fork()`에서 호출되는 `uvmcopy()`에서 lazy allocation으로 인해 발생하는 panic을 제거한다.

```c
/* kernel/vm.c */

int uvmcopy(pagetable_t old, pagetable_t new, uint64 sz) {
...
    for (i = 0; i < sz; i += PGSIZE) {
        if ((pte = walk(old, i, 0)) == 0)
            // panic("uvmcopy: pte should exist");
            continue;
        if ((*pte & PTE_V) == 0)
            // panic("uvmcopy: page not present");
            continue;
...
    }
...
}
```

![image](https://github.com/h0meb0dy/h0meb0dy/assets/104156058/5eb7ff5e-ea8d-4334-8050-34f00e38f2a7)

`sbrkbugs` 테스트에서 align되지 않은 size(`-(sz - 3500)`)를 `sbrk()`의 인자로 전달하여 panic이 발생한다.

```c
/* user/usertests.c */

// regression test. does the kernel panic if a process sbrk()s its
// size to be less than a page, or zero, or reduces the break by an
// amount too small to cause a page to be freed?
void
sbrkbugs(char *s)
{
...
  if(pid == 0){
    int sz = (uint64) sbrk(0);
    // set the break to somewhere in the very first
    // page; there used to be a bug that would incorrectly
    // free the first page.
    sbrk(-(sz - 3500));
    exit(0);
  }
...
}
```

`sys_sbrk()`에서 size를 align(round up)하도록 수정한다.

```c
/* kernel/sysproc.c */

uint64
sys_sbrk(void) {
...
    addr = p->sz;          // old size
    p->sz += PGROUNDUP(n); // increment (or decrement) process size
...
}
```

`sbrkbasic` 테스트에서 `sbrk(1)`를 여러 번 호출했을 때 같은 페이지에 할당되지 않으면 테스트가 실패한다.

```c
/* user/usertests.c */

void
sbrkbasic(char *s)
{
...
  // can one sbrk() less than a page?
  a = sbrk(0);
  for(i = 0; i < 5000; i++){
    b = sbrk(1);
    if(b != a){
      printf("%s: sbrk test failed %d %x %x\n", i, a, b);
      exit(1);
    }
    *b = 1;
    a = b + 1;
  }
...
}
```

`sys_sbrk()`에서 `p->sz`에는 `n`을 그대로 더하고 `uvmunmap()`에 들어가는 `p->sz`를 round down하여 처리하도록 수정한다.

```c
uint64
sys_sbrk(void) {
...
    addr = p->sz; // old size
    p->sz += n;   // increment (or decrement) process size
    if (n < 0) {
        uvmunmap(p->pagetable, PGROUNDDOWN(p->sz), (addr - PGROUNDDOWN(p->sz)) / PGSIZE, 1);
    }
    return addr;
}
```

`sbrkarg` 테스트에서 `sbrk()`로 할당받(았지만 아직 실제로 할당되지는 않)은 메모리에 `write()`를 시도하여 실패한다.

```c
/* user/usertests.c */

// test reads/writes from/to allocated memory
void
sbrkarg(char *s)
{
...
  // test writes to allocated memory
  a = sbrk(PGSIZE);
  if(pipe((int *) a) != 0){
    printf("%s: pipe() failed\n", s);
    exit(1);
  } 
}
```

`write` 등의 system call은 user의 virtual address를 받으면 `walkaddr()`로 physical address를 찾아서 접근한다. 따라서 `walkaddr()`에서 virtual address에 해당하는 physical address가 아직 할당되어 있지 않으면 할당하도록 수정한다. 단, 무조건 할당하는 것이 아니라 `p->sz`가 `va`보다 클 때, 즉 virtual address가 valid할 때만 할당한다.

```c
/* kernel/vm.c */

...
#include "spinlock.h"
#include "proc.h"
```

```c
/* kernel/vm.c */

uint64
walkaddr(pagetable_t pagetable, uint64 va) {
...
    if ((*pte & PTE_V) == 0) {
        struct proc *p = myproc();
        if (p->sz > va) {
            pa = (uint64)kalloc();
            if (!pa) {
                return 0;
            }
            memset((uint64 *)pa, 0, PGSIZE);
            if (mappages(p->pagetable, va, PGSIZE, (uint64)pa, PTE_W | PTE_X | PTE_R | PTE_U)) {
                kfree((uint64 *)pa);
                return 0;
            }
        } else {
            return 0;
        }
    }
...
}
```

`stacktest` 테스트에서 발생하는 `remap` panic과 `freewalk: leaf` panic을 제거한다.

```c
/* kernel/vm.c */

int mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm) {
...
    for (;;) {
...
        if (*pte & PTE_V) {
            // panic("remap");
            a += PGSIZE;
            pa += PGSIZE;
            continue;
        }
...
    }
    return 0;
}

void freewalk(pagetable_t pagetable) {
    // there are 2^9 = 512 PTEs in a page table.
    for (int i = 0; i < 512; i++) {
        pte_t pte = pagetable[i];
        if ((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0) {
...
        } else if (pte & PTE_V) {
            // panic("freewalk: leaf");
            continue;
        }
    }
    kfree((void *)pagetable);
}
```

![image](https://github.com/h0meb0dy/h0meb0dy/assets/104156058/6d9177c8-8260-4eaf-aa50-40bcde242544)
