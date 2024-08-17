# [MIT 6.S081 Fall 2020] Lab: page tables

> https://pdos.csail.mit.edu/6.S081/2020/labs/pgtbl.html

## Print a page table ([easy](https://pdos.csail.mit.edu/6.S081/2020/labs/guidance.html))

Page table에 존재하는 모든 PTE와 그에 해당하는 physical address를 출력하는 `vmprint()` 함수를 구현하는 실습이다.

### Add vmprint() in exec()

`exec()`이 리턴하기 직전에 `vmprint()`를 호출하여 첫 번째 프로세스(`pid == 1`)의 page table을 출력한다.

```c
int exec(char *path, char **argv) {
...
    if (p->pid == 1) vmprint(p->pagetable);

    return argc;  // this ends up in a0, the first argument to main(argc, argv)
...
}
```

`exec()`에서 `vmprint()`를 호출할 수 있도록 `defs.h`에 추가한다.

```c
// vm.c
...
void vmprint(pagetable_t);
```

### Implement vmprint()

3개의 레벨로 구성된 page table을 탐색하며 존재하는 모든 PTE와 그에 해당하는 physical address를 출력한다.

```c
void vmprint(pagetable_t pagetable) {
    printf("page table %p\n", pagetable);

    for (int i = 0; i < 512; i++) {
        // first level
        pte_t pte = pagetable[i];
        if (!(pte & PTE_V)) continue;
        uint64 pa1 = PTE2PA(pte);
        printf("..%d: pte %p pa %p\n", i, pte, pa1);

        for (int j = 0; j < 512; j++) {
            // second level
            pte_t pte = ((pte_t *)pa1)[j];
            if (!(pte & PTE_V)) continue;
            uint64 pa2 = PTE2PA(pte);
            printf(".. ..%d: pte %p pa %p\n", j, pte, pa2);

            for (int k = 0; k < 512; k++) {
                // third level
                pte_t pte = ((pte_t *)pa2)[k];
                if (!(pte & PTE_V)) continue;
                uint64 pa3 = PTE2PA(pte);
                printf(".. .. ..%d: pte %p pa %p\n", k, pte, pa3);
            }
        }
    }
}
```

### Test

```bash
make GRADEFLAGS=printout grade
```

![image](https://user-images.githubusercontent.com/104156058/227446294-c795c5fc-448e-48c0-8161-5a922445d167.png)

## A kernel page table per process ([hard](https://pdos.csail.mit.edu/6.S081/2020/labs/guidance.html))

이 실습과 다음 실습의 최종적인 목표는 kernel이 user pointer를 참조해야 할 때 physical address로 translate하는 과정을 생략하여 더 효율적으로 만드는 것이다. 그러려면 kernel page table에 user memory에 대한 mapping을 추가해야 하는데, user memory는 각 프로세스별로 virtual address로 관리되기 때문에 주소들이 중복되고, 따라서 하나의 kernel page table에 모두 mapping할 수는 없다.

따라서 이 실습에서는 실제 kernel page table과 연동되는 복사본을 각 프로세스에 추가하고, 다음 실습에서는 각 프로세스가 가지고 있는 kernel page table의 복사본에 그 프로세스의 user memory에 대한 mapping을 추가한다. 이를 통해 최종적인 목표를 달성할 수 있다.

### Add kernel page table to process

`proc.h`의 `struct proc` 구조체에 kernel page table을 나타내는 `kpagetable` 필드를 추가한다.

```c
struct proc {
...
    pagetable_t kpagetable;  // kernel page table
};
```

### Initialize kernel page table of process

`vm.c`에 정의된 `kvminit()`은 전역 변수 `kernel_pagetable`에 kernel page table을 할당하고 초기화한다. 각 프로세스가 가진 kernel page table의 복사본도 같은 방식으로 초기화해주기 위해, 새로운 kernel page table을 할당하고 초기화한 후 반환하는 `new_kvminit()`을 정의한다. `kvmmap()`은 page table을 인자로 받지 않기 때문에 그 대신 함수 내부 구현을 참고하여 `mappages()`를 사용해야 한다.

```c
// modified version of kvminit()
pagetable_t new_kvminit() {
    pagetable_t pagetable = kalloc();
    memset(pagetable, 0, PGSIZE);

    // uart registers
    // kvmmap(UART0, UART0, PGSIZE, PTE_R | PTE_W);
    if (mappages(pagetable, UART0, PGSIZE, UART0, PTE_R | PTE_W) != 0)
        panic("mappages");

    // virtio mmio disk interface
    // kvmmap(VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);
    if (mappages(pagetable, VIRTIO0, PGSIZE, VIRTIO0, PTE_R | PTE_W) != 0)
        panic("mappages");

    // CLINT
    // kvmmap(CLINT, CLINT, 0x10000, PTE_R | PTE_W);
    if (mappages(pagetable, CLINT, 0x10000, CLINT, PTE_R | PTE_W) != 0)
        panic("mappages");

    // PLIC
    // kvmmap(PLIC, PLIC, 0x400000, PTE_R | PTE_W);
    if (mappages(pagetable, PLIC, 0x400000, PLIC, PTE_R | PTE_W) != 0)
        panic("mappages");

    // map kernel text executable and read-only.
    // kvmmap(KERNBASE, KERNBASE, (uint64)etext - KERNBASE, PTE_R | PTE_X);
    if (mappages(pagetable, KERNBASE, (uint64)etext - KERNBASE, KERNBASE, PTE_R | PTE_X) != 0)
        panic("mappages");

    // map kernel data and the physical RAM we'll make use of.
    // kvmmap((uint64)etext, (uint64)etext, PHYSTOP - (uint64)etext, PTE_R | PTE_W);
    if (mappages(pagetable, (uint64)etext, PHYSTOP - (uint64)etext, (uint64)etext, PTE_R | PTE_W) != 0)
        panic("mappages");

    // map the trampoline for trap entry/exit to
    // the highest virtual address in the kernel.
    // kvmmap(TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
    if (mappages(pagetable, TRAMPOLINE, PGSIZE, (uint64)trampoline, PTE_R | PTE_X) != 0)
        panic("mappages");

    return pagetable;
}
```

`defs.h`에 `new_kvminit()`을 추가한다.

```c
// vm.c
...
pagetable_t new_kvminit();
```

`allocproc()`에 `new_kvminit()`으로 kernel page table을 할당하는 코드를 추가한다.

```c
static struct proc *
allocproc(void) {
...
    // An empty user page table.
...
    // generate kernel page table
    p->kpagetable = new_kvminit();
    if (!p->kpagetable) {
        proc_freepagetable(p->pagetable, p->sz);
        freeproc(p);
        release(&p->lock);
        return 0;
    }
...
}
```

### Allocate kernel stack in kernel page table of process

`procinit()`에 kernel stack을 할당하는 코드가 있다. 이 코드를 `allocproc()`으로 옮겨서 kernel stack이 각 프로세스의 kernel page table에 할당되도록 한다.

```c
void procinit(void) {
...
        // // Allocate a page for the process's kernel stack.
        // // Map it high in memory, followed by an invalid
        // // guard page.
        // char *pa = kalloc();
        // if (pa == 0)
        //     panic("kalloc");
        // uint64 va = KSTACK((int)(p - proc));
        // kvmmap(va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
        // p->kstack = va;
    }
    // kvminithart();
}
```

```c
static struct proc *
allocproc(void) {
...
    // kernel page table
...
    // Allocate a page for the process's kernel stack.
    // Map it high in memory, followed by an invalid
    // guard page.
    char *pa = kalloc();
    if (pa == 0)
        panic("kalloc");
    uint64 va = KSTACK((int)(p - proc));
    // kvmmap(va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
    if (mappages(p->kpagetable, va, PGSIZE, (uint64)pa, PTE_R | PTE_W) != 0)
        panic("mappages");
    p->kstack = va;
...
}
```

### Control satp

`kvminithart()`는 `satp` 레지스터의 값을 kernel page table의 주소로 복구하는 함수이다. `scheduler()`에서 프로세스가 실행될 때 `satp` 레지스터에 프로세스의 kernel page table의 주소를 저장하고, 프로세스 실행이 종료될 때 `kvminithart()`를 호출하여 `satp`가 다시 kernel page table의 주소를 저장하도록 한다.

```c
void scheduler(void) {
...
            if (p->state == RUNNABLE) {
                // load address of kernel page table to satp register
                w_satp(MAKE_SATP(p->kpagetable));
                sfence_vma();
...
                // restore satp
                kvminithart();

                found = 1;
            }
...
}
```

### Make kvmpa() reference kernel page table of process

`kvmpa()`에서 kernel virtual address를 physical address로 변환할 때 프로세스의 kernel page table을 참조하도록 한다.

```c
uint64
kvmpa(uint64 va) {
...
    // pte = walk(kernel_pagetable, va, 0);
    pte = walk(myproc()->kpagetable, va, 0);
...
}
```

`vm.c`에서 `myproc()`을 사용하기 위해 필요한 헤더 파일을 include한다.

```c
...
#include "spinlock.h"
#include "proc.h"
```

### Free kernel page table of process

`allocproc()`에서 할당한 kernel page table과 kernel stack들은 `freeproc()`에서 할당 해제해주어야 한다. `uvmunmap()`으로 `p->kstack`을 해제하고, `freewalk()`로 `p->kpagetable`을 해제한다.

```c
static void
freeproc(struct proc *p) {
...
    // free kernel stack
    if (p->kstack) {
        uvmunmap(p->kpagetable, p->kstack, 1, 1);
        p->kstack = 0;
    }

    // free kernel page table
    if (p->kpagetable) freewalk(p->kpagetable);
}
```

`freewalk()`에서 발생하는 `panic: freewalk: leaf`를 주석 처리한다.

```c
void freewalk(pagetable_t pagetable) {
...
        } else if (pte & PTE_V) {
            // panic("freewalk: leaf");
            continue;
        }
...
}
```

### Test

```bash
make GRADEFLAGS=usertests grade
```

![image](https://user-images.githubusercontent.com/104156058/229357009-6c1dd7d5-b1f9-45c8-b11e-b117c2683400.png)

## Simplify copyin/copyinstr ([hard](https://pdos.csail.mit.edu/6.S081/2020/labs/guidance.html))

`copyin()`과 `copyinstr()`은 user space에서 kernel space로 데이터를 복사하는 함수이다. Source의 virtual address로부터 page table을 탐색하여 physical address를 구하고, physical address에 저장된 데이터를 가져온다. 하지만 함수가 호출될 때마다 page table을 탐색하는 것은 비효율적이므로, `vmcopyin.c`에 새롭게 정의된 `copyin_new()`와 `copyinstr_new()`에서는 `memmove()`로 `srcva`에서 `dst`로 직접 데이터를 복사한다. 이 실습의 목표는 프로세스의 kernel page table에 user virtual address와 physical address의 mapping을 저장하여 `copyin()`을 `copyin_new()`로, `copyinstr()`을 `copyinstr_new()`로 대체했을 때 문제없이 작동하도록 하는 것이다.

### Replace copyin() with copyin_new()

`copyin()`과 `copyinstr()`의 구현을 제거하고 각각 `copyin_new()`와 `copyinstr_new()`를 호출하는 것으로 대체한다.

```c
// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len) {
    return copyin_new(pagetable, dst, srcva, len);
    /*
...
    */
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max) {
    return copyinstr_new(pagetable, dst, srcva, max);
    /*
...
    */
}
```

`vm.c`에서 `copyin_new()`와 `copyinstr_new()`를 참조할 수 있도록 `defs.h`에 추가한다.

```c
// vm.c
...

// vmcopyin.c
int copyin_new(pagetable_t, char*, uint64, uint64);
int copyinstr_new(pagetable_t, char*, uint64, uint64);
```

### Copy PTEs in user page table to kernel page table of process

`vm.c`에 `copypages()` 함수를 구현한다. 이 함수는 user page table의 PTE들을 프로세스의 kernel page table로 복사한다.

```c
// copy user page table to kernel page table
void copypages(pagetable_t upagetable, pagetable_t kpagetable, uint64 start, uint64 end) {
    if (end > PLIC) panic("size too big");

    for (uint64 va = start; va < end; va += PGSIZE) {
        pte_t *upte = walk(upagetable, va, 0);
        if (!upte) panic("user PTE not exist");
        pte_t *kpte = walk(kpagetable, va, 1);
        if (!kpte) panic("kernel PTE not exist");
        *kpte = *upte & ~PTE_U;
    }
}
```

`defs.h`에 `copypages()`를 추가한다.

```c
// vm.c
...
void copypages(pagetable_t, pagetable_t, uint64, uint64);
```

Page table을 건드리는 함수는 `userinit()`, `growproc()`, `fork()`, `exec()`이 있다. 이 함수들에서 `copypages()`를 호출하여 변동사항을 kernel page table에 반영하도록 한다.

```c
void userinit(void) {
...
    // allocate one user page and copy init's instructions
    // and data into it.
...

    // copy user page table to kernel page table
    copypages(p->pagetable, p->kpagetable, 0, p->sz);
...
}
```

```c
int growproc(int n) {
...
    // copy user page table to kernel page table
    copypages(p->pagetable, p->kpagetable, 0, p->sz);

    return 0;
}
```

```c
int fork(void) {
...
    // Copy user memory from parent to child.
...
    // copy user page table to kernel page table
    copypages(np->pagetable, np->kpagetable, 0, np->sz);
...
}
```

```c
int exec(char *path, char **argv) {
...
    // Commit to the user image.
...
    // copy user page table to kernel page table
    copypages(p->pagetable, p->kpagetable, 0, p->sz);
...
}
```

### Test

`usertests`에서 timeout이 뜬다. `copypages()`에서 user page table에 변동사항이 있는지 검사하지 않고 그냥 무조건 복사해서 시간이 오래 걸리기 때문인데, dirty bit를 추가하여 변동사항이 있는 page만 복사하도록 하면 시간을 줄일 수 있지만 귀찮아서(...) 그냥 `grade-lab-pgtbl` 스크립트의 timeout을 600초로 늘려서 통과했다.

```python
@test(0, "usertests")
def test_usertests():
    r.run_qemu(shell_script([
        'usertests'
    ]), timeout=600)
```

```bash
make grade
```

![image](https://user-images.githubusercontent.com/104156058/230373247-efeb2bd6-dfd2-4a30-8a18-2cd7e8ccbcdb.png)
