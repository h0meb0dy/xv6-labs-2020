# [MIT 6.S081 Fall 2020] Lab: Copy-on-Write Fork for xv6

> https://pdos.csail.mit.edu/6.S081/2020/labs/cow.html

Xv6의 `fork()` system call은 parent process의 모든 user-space memory를 child로 copy한다. Parent의 memory가 클수록 시간이 오래 걸릴 것이고, 만약 copy한 memory가 child에서 실제로 사용되지 않는다면 매우 비효율적인 작업이 된다. Copy-on-write(COW) fork의 목표는 memory가 실제로 필요한 시점에 copy하여 시간과 공간을 절약하는 것이다.

## Implement copy-on write

실제로 copy가 진행되기 전에는 page에 대한 reference count를 증가시켜서, 나중에 사용될 예정인 memory가 free되지 않도록 해야 한다.

`kalloc.c`에 reference count를 구현한다.

```c
/* kernel/kalloc.c */

int refcnt[PHYSTOP / PGSIZE] = {0};

// return reference count of page
int get_refcnt(uint64 va) {
    return refcnt[va / PGSIZE];
}

// increase reference count of page
void inc_refcnt(uint64 va) {
    refcnt[va / PGSIZE]++;
}

// decrease reference count of page
void dec_refcnt(uint64 va) {
    refcnt[va / PGSIZE]--;
}

void kfree(void *pa) {
...
    refcnt[(uint64)r / PGSIZE] = 0;
    release(&kmem.lock);
}

void *
kalloc(void) {
...
    refcnt[(uint64)r / PGSIZE] = 1;
    release(&kmem.lock);
...
}
```

구현한 함수들은 `defs.h`에 추가한다.

```c
/* kernel/defs.h */

// kalloc.c
...
int get_refcnt(uint64);
void inc_refcnt(uint64);
void dec_refcnt(uint64);
```

`uvmunmap()`에서 unmap할 때 reference count를 감소시키고, 메모리를 해제하기 전에 reference count가 0인지 검사하도록 한다.

```c
/* kernel/vm.c */

void uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free) {
...
    for (a = va; a < va + npages * PGSIZE; a += PGSIZE) {
...
        uint64 pa = PTE2PA(*pte);
        dec_refcnt(pa);
        if (do_free) {
            if (!get_refcnt(pa)) {
                kfree((void *)pa);
            }
        }
        *pte = 0;
    }
}
```

Trap 발생 시 COW로 인한 것인지 확인할 수 있도록 `riscv.h`에서 PTE의 RSW(reserved for supervised software) 영역에 `PTE_COW` 플래그를 추가한다.

```c
/* kernel/riscv.h */

#define PTE_V (1L << 0) // valid
#define PTE_R (1L << 1)
#define PTE_W (1L << 2)
#define PTE_X (1L << 3)
#define PTE_U (1L << 4) // 1 -> user can access
#define PTE_COW (1L << 8) // COW mapping flag
```

`uvmcopy()`에서 child의 메모리를 바로 할당하는 것이 아니라, child의 PTE가 실제로는 parent의 physical address와 map되도록 수정한다. 이때 parent와 child 모두에서 `PTE_W`(쓰기 권한)를 삭제하고 `PTE_COW`(COW mapping flag)를 추가하고, physical address의 reference count를 증가시킨다.

```c
/* kernel/vm.c */

int uvmcopy(pagetable_t old, pagetable_t new, uint64 sz) {
...
    for (i = 0; i < sz; i += PGSIZE) {
...
        pa = PTE2PA(*pte);
        flags = PTE_FLAGS(*pte);
        *pte &= ~PTE_W;  // remove write permission
        *pte |= PTE_COW; // add COW mapping flag
        // if ((mem = kalloc()) == 0)
        //     goto err;
        // memmove(mem, (char *)pa, PGSIZE);
        mem = (void *)pa;
        inc_refcnt((uint64)mem);
        if (mappages(new, i, PGSIZE, (uint64)mem, (flags & ~PTE_W) | PTE_COW) != 0) {
            kfree(mem);
            goto err;
        }
    }
...
}
```

`usertrap()`에서 `scause`가 15이고 `PTE_COW` 플래그가 켜져 있을 경우, 메모리를 새로 할당하여 write 시도를 한 프로세스에게 할당한다. 이때 `PTE_W`를 다시 추가하고 `PTE_COW`를 제거한다.

```c
/* kernel/trap.c */

void usertrap(void) {
...
    if (r_scause() == 8) {
...
    } else if ((which_dev = devintr()) != 0) {
        // ok
    } else if (r_scause() == 15) {
        uint64 va = PGROUNDDOWN(r_stval()); // address of faulted page

        pte_t *pte = walk(p->pagetable, va, 0);
        if (!pte) {
            panic("usertrap(): pte should exist");
        }
        if (!(*pte & PTE_V)) {
            panic("usertrap(): page not present");
        }

        if (*pte & PTE_COW) {
            uint64 pa = PTE2PA(*pte);
            uint64 flags = PTE_FLAGS(*pte);
            *pte |= PTE_W;    // restore write permission
            *pte &= ~PTE_COW; // remove COW mapping flag

            void *mem = kalloc();
            memmove(mem, (void *)pa, PGSIZE);

            uvmunmap(p->pagetable, va, 1, 1);
            if (mappages(p->pagetable, va, PGSIZE, (uint64)mem, (flags & ~PTE_COW) | PTE_W)) {
                kfree(mem);
                uvmunmap(p->pagetable, va, 1, 1);
                p->killed = 1;
            }
        }
    } else {
...
    }
...
}
```

`trap.c`에서 `walk()`를 호출할 수 있도록 `defs.h`에 추가한다.

```c
/* kernel/defs.h */

// vm.c
...
pte_t *walk(pagetable_t, uint64, int);
```

`filetest` 테스트에서 `fork()` 이후에 parent에서 child로 4바이트 데이터를 주고받는 작업을 수행한다.

```c
/* user/cowtest.c */

// test whether copyout() simulates COW faults.
void
filetest()
{
  printf("file: ");
  
  buf[0] = 99;

  for(int i = 0; i < 4; i++){
...
    if(pid == 0){
      sleep(1);
      if(read(fds[0], buf, sizeof(i)) != sizeof(i)){
        printf("error: read failed\n");
        exit(1);
      }
      sleep(1);
      int j = *(int*)buf;
      if(j != i){
        printf("error: read the wrong value\n");
        exit(1);
      }
      exit(0);
    }
    if(write(fds[1], &i, sizeof(i)) != sizeof(i)){
      printf("error: write failed\n");
      exit(-1);
    }
  }
...
}
```

데이터는 parent에서 `copyin()`을 통해 kernel로 갔다가 `copyout()`을 통해 child로 가게 되는데, `copyout()`이 child의 메모리에 쓰려고 시도할 때 COW로 인해 parent와 공유하고 있는 physical address에 `PTE_W` 권한이 없어서 데이터를 정상적으로 받을 수 없다.

`copyout()`에서도 `usertrap()`에서처럼 COW로 인한 trap을 따로 처리하도록 하여 destination의 physical address를 수정하도록 한다.

```c
/* kernel/vm.c */

int copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len) {
    uint64 n, va0, pa0;

    while (len > 0) {
        va0 = PGROUNDDOWN(dstva);
        // pa0 = walkaddr(pagetable, va0);
        pte_t *pte = walk(pagetable, va0, 0);
        pa0 = PTE2PA(*pte);
        if (pa0 == 0)
            return -1;

        if (*pte & PTE_COW) {
            uint64 flags = PTE_FLAGS(*pte);
            *pte |= PTE_W;    // restore write permission
            *pte &= ~PTE_COW; // remove COW mapping flag

            void *mem = kalloc();
            memmove(mem, (void *)pa0, PGSIZE);
            pa0 = (uint64)mem;

            uvmunmap(pagetable, va0, 1, 1);
            if (mappages(pagetable, va0, PGSIZE, (uint64)mem, (flags & ~PTE_COW) | PTE_W)) {
                kfree(mem);
                uvmunmap(pagetable, va0, 1, 1);
            }
        }
...
    }
    return 0;
}
```

![image](https://github.com/h0meb0dy/h0meb0dy/assets/104156058/e09991cc-5caf-495d-9f70-462939869008)

`execout` 테스트는 할당 가능한 메모리가 남아 있지 않은 상태에서 panic이 발생하지 않는지 확인한다.

```c
/* user/usertests.c */

// test the exec() code that cleans up if it runs out
// of memory. it's really a test that such a condition
// doesn't cause a panic.
void
execout(char *s)
{
  for(int avail = 0; avail < 15; avail++){
    int pid = fork();
    if(pid < 0){
      printf("fork failed\n");
      exit(1);
    } else if(pid == 0){
      // allocate all of memory.
      while(1){
        uint64 a = (uint64) sbrk(4096);
        if(a == 0xffffffffffffffffLL)
          break;
        *(char*)(a + 4096 - 1) = 1;
      }

      // free a few pages, in order to let exec() make some
      // progress.
      for(int i = 0; i < avail; i++)
        sbrk(-4096);
      
      close(1);
      char *args[] = { "echo", "x", 0 };
      exec("echo", args);
      exit(0);
    } else {
      wait((int*)0);
    }
  }

  exit(0);
}
```

`usertrap()`과 `copyout()`에서 새로운 메모리를 할당하는 부분에서 각각 `kalloc()`의 반환값을 보고 메모리 할당이 성공했는지 확인하도록 한다.

```c
/* kernel/trap.c */

void usertrap(void) {
...
    if (r_scause() == 8) {
...
    } else if ((which_dev = devintr()) != 0) {
        // ok
    } else if (r_scause() == 15) {
...
        if (*pte & PTE_COW) {
...
            void *mem = kalloc();
            if (!mem) {
                p->killed = 1;
            } else {
                memmove(mem, (void *)pa, PGSIZE);

                uvmunmap(p->pagetable, va, 1, 1);
                if (mappages(p->pagetable, va, PGSIZE, (uint64)mem, (flags & ~PTE_COW) | PTE_W)) {
                    kfree(mem);
                    uvmunmap(p->pagetable, va, 1, 1);
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

```c
/* kernel/vm.c */

int copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len) {
    uint64 n, va0, pa0;

    while (len > 0) {
...
        if (*pte & PTE_COW) {
...
            void *mem = kalloc();
            if (!mem) {
                return 0;
            }
...
        }
...
    }
    return 0;
}
```

`copyout` 테스트는 system call에 이상한 주소(`0x80000000`, `0xffffffffffffffff`)를 넣었을 때 system call이 실패하는지 확인한다.

```c
/* user/usertests.c */

// what if you pass ridiculous pointers to system calls
// that write user memory with copyout?
void
copyout(char *s)
{
  uint64 addrs[] = { 0x80000000LL, 0xffffffffffffffff };

  for(int ai = 0; ai < 2; ai++){
    uint64 addr = addrs[ai];

    int fd = open("README", 0);
    if(fd < 0){
      printf("open(README) failed\n");
      exit(1);
    }
    int n = read(fd, (void*)addr, 8192);
    if(n > 0){
      printf("read(fd, %p, 8192) returned %d, not -1 or 0\n", addr, n);
      exit(1);
    }
    close(fd);

    int fds[2];
    if(pipe(fds) < 0){
      printf("pipe() failed\n");
      exit(1);
    }
    n = write(fds[1], "x", 1);
    if(n != 1){
      printf("pipe write failed\n");
      exit(1);
    }
    n = read(fds[0], (void*)addr, 8192);
    if(n > 0){
      printf("read(pipe, %p, 8192) returned %d, not -1 or 0\n", addr, n);
      exit(1);
    }
    close(fds[0]);
    close(fds[1]);
  }
}
```

`copyout()`에서 인자로 받은 `va0`로부터 `walk()`로 구한 `pte`가 0인지, 즉 `va0`가 invalid한 주소인지 확인하도록 한다. 만약 `pte`가 0이면 error를 의미하는 `-1`을 반환한다.

```c
/* kernel/vm.c */

int copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len) {
    uint64 n, va0, pa0;

    while (len > 0) {
        va0 = PGROUNDDOWN(dstva);
        // pa0 = walkaddr(pagetable, va0);
        pte_t *pte = walk(pagetable, va0, 0);
        if (!pte) {
            return -1;
        }
...
    }
    return 0;
}
```

`walk()`에서 인자로 받은 `va`가 `MAXVA`보다 클 때 발생하는 panic을 제거한다.

```c
/* kernel/vm.c */

pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc) {
    if (va >= MAXVA) {
        // panic("walk");
        return 0;
    }
...
}
```

![image](https://github.com/h0meb0dy/h0meb0dy/assets/104156058/6eab0fa8-85b1-4139-b967-68589027ece5)
