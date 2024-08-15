# [MIT 6.S081 Fall 2020] Lab: system calls

> https://pdos.csail.mit.edu/6.S081/2020/labs/syscall.html

## System call tracing ([moderate](https://pdos.csail.mit.edu/6.S081/2020/labs/guidance.html))

리눅스의 `strace`와 같이 시스템 콜을 추적하는 프로그램을 구현하는 실습입니다.

### Add trace to Makefile

미리 구현되어 있는 `user/trace.c`가 컴파일될 수 있도록 `Makefile`에 추가합니다.

```makefile
...
UPROGS=\
...
	$U/_trace\
...
```

### Add trace system call

`trace.c`에서 호출하는 `trace` 시스템 콜을 커널에 추가합니다.

`syscall.h`에 시스템 콜 번호를 정의합니다.

```c
// System call numbers
...
#define SYS_trace 22
```

`syscall.c`에 `trace` 시스템 콜의 구현체인 `sys_trace()` 함수의 원형을 추가하고, 이 함수를 시스템 콜 함수들의 주소가 저장된 배열인 `syscalls`에 추가합니다.

```c
extern uint64 sys_chdir(void);
...
extern uint64 sys_trace(void);

static uint64 (*syscalls[])(void) = {
...
    [SYS_close] sys_close,
    [SYS_trace] sys_trace
};
```

그리고 `trace`에서 추적하는 시스템 콜들의 이름이 필요하기 때문에, 문자열 형태로 저장해둡니다. 편의를 위해 n번 시스템 콜의 이름이 `syscall_name[n]`에 저장되도록 합니다. 

```c
char *syscall_name[] = {"", "fork", "exit", "wait", "pipe", "read", "kill", "exec", "fstat", "chdir", "dup", "getpid", "sbrk", "sleep", "uptime", "open", "write", "mknod", "unlink", "link", "mkdir", "close", "trace"};
```

User가 `trace()` 시스템 콜을 호출할 수 있도록 `user.h`에 `trace()` 함수의 원형을 추가합니다. `trace.c`에서 `if (trace(atoi(argv[1])) < 0)`의 형식으로 호출하고 있기 때문에, 정수를 인자로 받아서 정수를 반환하는 함수로 선언합니다.

```c
// system calls
...
int trace(int);
```

`sysproc.c`에 `sys_trace()` 함수를 추가합니다. 일단 임시로 0을 반환하는 빈 함수로 정의합니다.

```c
uint64 sys_trace(void) {
    return 0;
}
```

`usys.pl`에 `trace`를 추가하여 user의 `trace()` 함수와 커널의 `trace` 시스템 콜을 연결합니다.

```c
entry("fork");
...
entry("trace");
```

### Implement trace system call

`trace` 시스템 콜은 추적할 시스템 콜의 종류를 bitmask의 형태로 저장합니다. `struct proc` 구조체에 `tracemask` 필드를 추가해서, 프로세스가 시스템 콜을 호출할 때마다 이 `tracemask`에 해당하는지 검사하고 출력하는 식으로 구현할 수 있습니다.

`proc.h`의 `struct proc` 구조체에 `tracemask` 필드를 추가합니다.

```c
struct proc {
...
    int tracemask;
};
```

모든 시스템 콜은 `syscall.c`의 `syscall()`을 거칩니다. 이 함수에서 `syscalls` 배열을 참조하여 각 시스템 콜에 해당하는 함수를 호출합니다. 이 호출 직후에, 만약 시스템 콜 번호가 `tracemask`에 설정되어 있다면 시스템 콜의 정보를 포맷에 맞게 출력하도록 합니다. 시스템 콜의 반환값은 `a0` 레지스터에 저장되는데, 이 레지스터의 값은 `p->trapframe->a0`로 참조할 수 있습니다.

```c
void syscall(void) {
...
    if (num > 0 && num < NELEM(syscalls) && syscalls[num]) {
        p->trapframe->a0 = syscalls[num]();
        if (p->tracemask >> num & 1)
            printf("%d: syscall %s -> %d\n", p->pid, syscall_name[num], p->trapframe->a0);
...
}
```

`sysproc.c`에 정의했던 `sys_trace()`를 구현합니다. 인자로 `tracemask`를 받아서 현재 프로세스 구조체에 저장합니다.

```c
uint64 sys_trace(void) {
    int tracemask;

    // fetch argument
    if (argint(0, &tracemask)) return -1;

    myproc()->tracemask = tracemask;

    return 0;
}
```

`proc.c`의 `fork()`에 `tracemask`를 복사하는 코드를 추가합니다.

```c
int fork(void) {
...
    np->tracemask = p->tracemask;

    release(&np->lock);

    return pid;
}
```

### Test

```bash
make GRADEFLAGS=trace grade
```

![image](https://user-images.githubusercontent.com/104156058/227399717-a03ac483-92c4-4ad4-b87e-99cd9d77ad3c.png)

## Sysinfo ([moderate](https://pdos.csail.mit.edu/6.S081/2020/labs/guidance.html))

현재 CPU의 free memory의 양과 프로세스 개수 정보를 담은 `struct sysinfo` 구조체(`sysinfo.h`에 정의)를 반환하는 `sysinfo` 시스템 콜을 구현하는 실습입니다.

### Add sysinfotest to Makefile

`sysinfotest`가 컴파일될 수 있도록 `Makefile`에 추가합니다.

```makefile
...
UPROGS=\
...
	$U/_trace\
	$U/_sysinfotest\
...
```

### Add sysinfo system call

`sysinfotest.c`에서 호출하는 `sysinfo` 시스템 콜을 커널에 추가합니다.

`syscall.h`에 시스템 콜 번호를 정의합니다.

```c
// System call numbers
...
#define SYS_trace 22
#define SYS_sysinfo 23
```

`syscall.c`에 `sysinfo` 시스템 콜의 구현체인 `sys_sysinfo()` 함수의 원형을 추가하고, 이 함수를 시스템 콜 함수들의 주소가 저장된 배열인 `syscalls`에 추가합니다.

```c
...
extern uint64 sys_trace(void);
extern uint64 sys_sysinfo(void);

static uint64 (*syscalls[])(void) = {
...
    [SYS_trace] sys_trace,
    [SYS_sysinfo] sys_sysinfo};
```

`trace`에서 사용하는 시스템 콜 이름 목록에 `sysinfo`를 추가합니다.

```c
char *syscall_name[] = {"", "fork", "exit", "wait", "pipe", "read", "kill", "exec", "fstat", "chdir", "dup", "getpid", "sbrk", "sleep", "uptime", "open", "write", "mknod", "unlink", "link", "mkdir", "close", "trace", "sysinfo"};
```

User가 `sysinfo()` 시스템 콜을 호출할 수 있도록 `user.h`에 `sysinfo()` 함수의 원형을 추가합니다. `sysinfotest.c`에서 `if (sysinfo(info) < 0)`의 형식으로 호출하고 있기 때문에, `struct sysinfo`형 포인터를 인자로 받아서 정수를 반환하는 함수로 선언합니다.

```c
// system calls
...
int trace(int);
int sysinfo(struct sysinfo*);
```

이때 `struct sysinfo`가 정의되어 있지 않다는 오류가 발생하는데, `user.h`의 맨 위에서 `struct sysinfo`를 추가로 선언하여 해결해줍니다.

```c
struct stat;
struct rtcdate;
struct sysinfo;
```

`sysproc.c`에 `sys_sysinfo()` 함수를 추가합니다. 일단 임시로 0을 반환하는 빈 함수로 정의합니다.

```c
uint64 sys_sysinfo(void) {
    return 0;
}
```

`usys.pl`에 `sysinfo`를 추가하여 user의 `sysinfo()` 함수와 커널의 `sysinfo` 시스템 콜을 연결합니다.

```c
...
entry("trace");
entry("sysinfo");
```

### Implement get_freemem()

`kalloc.c`에서는 free memory를 single linked list 형식으로 관리합니다.

```c
struct run {
    struct run *next;
};

struct {
    struct spinlock lock;
    struct run *freelist;
} kmem;
```

`freelist`부터 시작해서 `next`를 따라가면서 `PGSIZE`씩 더하면 현재 free memory의 총합을 알 수 있습니다. `kalloc.c`에 `get_freemem()` 함수를 추가하여 이 과정을 구현합니다.

```c
// return amount of free memory (bytes)
uint64 get_freemem() {
    uint64 freemem = 0;
    for (struct run *ptr = kmem.freelist; ptr; ptr = ptr->next)
        freemem += PGSIZE;
    return freemem;
}
```

`sys_sysinfo()`에서 `get_freemem()`을 사용할 수 있도록 `defs.h`에 추가합니다.

```c
// kalloc.c
...
uint64 get_freemem(void);
```

### Implement get_nproc()

`proc.c`에서는 프로세스들의 정보를 배열에 담아 관리합니다.

```c
struct proc proc[NPROC];
```

`proc.h`를 보면 프로세스는 총 5가지 상태를 가질 수 있는데,

```c
enum procstate { UNUSED,
                 SLEEPING,
                 RUNNABLE,
                 RUNNING,
                 ZOMBIE };
```

`sysinfo`는 `UNUSED` 상태가 아닌 프로세스들의 개수를 반환해야 합니다. `proc.c`에 `get_nproc()` 함수를 추가하여 이 과정을 구현합니다.

```c
// get number of processes
uint64 get_nproc() {
    uint64 nproc = 0;
    for (int i = 0; i < NPROC; i++)
        if (proc[i].state != UNUSED) nproc++;
    return nproc;
}
```

`sys_sysinfo()`에서 `get_nproc()`을 사용할 수 있도록 `defs.h`에 추가합니다.

```c
// proc.c
...
uint64 get_nproc(void);
```

### Implement sysinfo system call

`sysproc.c`에 정의했던 `sys_sysinfo()`를 구현합니다. `struct sysinfo` 구조체 포인터를 인자로 받아서, 그 포인터에 `freemem`과 `nproc`을 채웁니다. 이때 kernel space에서 user space로 데이터를 직접 쓰려고 하면 kerneltrap이 발생하기 때문에 `copyout()`을 사용해서 데이터를 복사해야 합니다.

```c
uint64 sys_sysinfo(void) {
    struct sysinfo *sysinfo_user;

    // fetch argument
    argaddr(0, (void *)&sysinfo_user);

    struct sysinfo sysinfo = {get_freemem(), get_nproc()};

    // copy sysinfo to user space
    if (copyout(myproc()->pagetable, (uint64)sysinfo_user, (char *)&sysinfo, sizeof(sysinfo)) < 0)
        return -1;

    return 0;
}
```

### Test

```bash
make GRADEFLAGS=sysinfo grade
```

![image](https://user-images.githubusercontent.com/104156058/227404879-871af4dc-b146-4d9d-9cf3-909363b1f3d4.png)
