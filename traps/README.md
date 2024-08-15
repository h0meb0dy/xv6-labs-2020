# [MIT 6.S081 Fall 2020] Lab: traps

> https://pdos.csail.mit.edu/6.S081/2020/labs/traps.html

## RISC-V assembly ([easy](https://pdos.csail.mit.edu/6.S081/2020/labs/guidance.html))

`user/call.c`가 컴파일되어 생성되는 `user/call.asm`을 보고 몇 가지의 질문에 답하는 실습입니다.

```c
#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int g(int x) {
  return x+3;
}

int f(int x) {
  return g(x);
}

void main(void) {
  printf("%d %d\n", f(8)+1, 13);
  exit(0);
}
```

Q. Which registers contain arguments to functions? For example, which register holds 13 in main's call to `printf`?

A. `main()`에서 호출하는 `printf()`의 첫 번째 인자는 `f(8) + 1`이므로 12가 되고, 두 번째 인자는 13입니다.

```
printf("%d %d\n", f(8)+1, 13);
24:	4635                	li	a2,13
26:	45b1                	li	a1,12
28:	00000517          	auipc	a0,0x0
2c:	7b050513          	addi	a0,a0,1968 # 7d8 <malloc+0xea>
30:	00000097          	auipc	ra,0x0
34:	600080e7          	jalr	1536(ra) # 630 <printf>
```

24 라인과 26 라인을 보면, 첫 번째 인자인 12는 `a1`에 들어가고, 두 번째 인자인 13은 `a2`에 들어가는 것을 알 수 있습니다.

Q. Where is the call to function `f` in the assembly code for main? Where is the call to `g`? (Hint: the compiler may inline functions.)

A. `make qemu`를 실행했을 때 출력되는 컴파일 명령어들 중에서 `call.c`를 컴파일하는 부분을 찾아보면 다음과 같습니다.

```bash
riscv64-linux-gnu-gcc -Wall -Werror -O -fno-omit-frame-pointer -ggdb -DSOL_TRAPS -DLAB_TRAPS -MD -mcmodel=medany -ffreestanding -fno-common -nostdlib -mno-relax -I. -fno-stack-protector -fno-pie -no-pie   -c -o user/call.o user/call.c
riscv64-linux-gnu-ld -z max-page-size=4096 -N -e main -Ttext 0 -o user/_call user/call.o user/ulib.o user/usys.o user/printf.o user/umalloc.o
riscv64-linux-gnu-objdump -S user/_call > user/call.asm
```

첫 번째 줄에서 `gcc`의 최적화 옵션인 `-O` 옵션을 확인할 수 있습니다. 이 옵션 때문에 `f()`가 인라인 함수로 최적화되어 들어가게 됩니다.

로컬에서 테스트해보면, `-O` 옵션을 빼고 돌리면 `f()`를 호출하는 과정이 추가되는 것을 확인할 수 있습니다.

Q. At what address is the function `printf` located?

A. `printf()`는 `0x630`에 위치합니다.

```
0000000000000630 <printf>:

void
printf(const char *fmt, ...)
{
...
```

Q. What value is in the register `ra` just after the `jalr` to `printf` in `main`?

A. `ra`는 return address를 저장하는 레지스터로, 함수 호출 후 돌아올 주소를 저장합니다.

```
34:	600080e7          	jalr	1536(ra) # 630 <printf>
exit(0);
38:	4501                	li	a0,0
```

따라서 `printf()` 호출 직후에 `ra`에는 `0x38`이 저장됩니다.

Q. Run the following code.

```c
	unsigned int i = 0x00646c72;
	printf("H%x Wo%s", 57616, &i);
```

What is the output? [Here's an ASCII table](http://web.cs.mun.ca/~michael/c/ascii-table.html) that maps bytes to characters.

The output depends on that fact that the RISC-V is little-endian. If the RISC-V were instead big-endian what would you set `i` to in order to yield the same output? Would you need to change `57616` to a different value?

[Here's a description of little- and big-endian](http://www.webopedia.com/TERM/b/big_endian.html) and [a more whimsical description](http://www.networksorcery.com/enp/ien/ien137.txt).

A. 57616은 16진수로 `0xe110`이고, `i`는 리틀 엔디안에서 메모리에 `72 6c 64 00`으로 저장되기 때문에 문자열로 표현하면 `"rld"`가 됩니다. 따라서 리틀 엔디안 시스템인 RISC-V에서는 `"He110 World"`가 출력됩니다.

빅 엔디안에서는 `i`가 메모리에 `00 64 6c 72`로 저장됩니다. 따라서 `%s`로 출력했을 때 가장 먼저 널 문자를 만나기 때문에 `"He110 Wo"`가 출력됩니다. 57616의 경우 `%x`로 16진수를 출력하기 때문에 리틀 엔디안과 빅 엔디안에서 출력되는 값은 다르지 않습니다.

Q. In the following code, what is going to be printed after `'y='`? (note: the answer is not a specific value.) Why does this happen?

```c
	printf("x=%d y=%d", 3);
```

A. `printf()`의 두 번째 `%d`는 함수의 세 번째 인자를 저장하는 레지스터에서 값을 가져와서 정수 형식으로 출력하는데, 코드에서 세 번째 인자를 정해주지 않았기 때문에 원래 레지스터에 저장되어 있던 쓰레기값을 출력하게 됩니다. 따라서 정확하게 예측할 수 없습니다.

## Backtrace ([moderate](https://pdos.csail.mit.edu/6.S081/2020/labs/guidance.html))

`sys_sleep()` 내부에 backtrace를 구현하는 실습입니다. 스택의 frame pointer를 따라가면서 return address를 찾아서 출력하는 과정을 반복하면 됩니다.

### Add r_fp() to riscv.h

힌트의 지시사항대로, `riscv.h`에 frame pointer를 읽어오는 `r_fp()` 함수를 추가합니다.

```c
static inline uint64
r_fp() {
    uint64 x;
    asm volatile("mv %0, s0"
                 : "=r"(x));
    return x;
}
```

### Implement backtrace()

제공된 [lecture notes](https://pdos.csail.mit.edu/6.828/2020/lec/l-riscv-slides.pdf)에 따르면 return address는 frame pointer보다 한 칸 낮은 주소에 위치합니다.

![image](https://user-images.githubusercontent.com/104156058/230921893-30e41818-8912-4e90-9a44-61346af91f60.png)

따라서 `r_fp()`로 frame pointer의 값을 가져와서 8을 빼면 return address의 주소가 됩니다. 그 주소에 있는 값이 return address일텐데, 만약 kernel base보다 작으면 불가능한 주소이므로 backtrace를 종료합니다.

`printf.c`에 `backtrace()`를 구현합니다.

```c
void backtrace() {
    printf("backtrace:\n");
    uint64 fp = r_fp();  // current frame pointer

    while (fp != 0) {
        uint64 ret = *((uint64 *)fp - 1);
        if (ret < KERNBASE)  // if not valid kernel address
        {
            break;
        }
        printf("%p\n", ret);
        fp = *((uint64 *)fp - 2);
    }
}
```

`defs.h`에 `backtrace()`를 추가합니다.

```c
// printf.c
...
void backtrace(void);
```

`sys_sleep()`에서 `backtrace()`를 호출합니다.

```c
uint64
sys_sleep(void) {
...
    backtrace();
    return 0;
}
```

### Test

```bash
make GRADEFLAGS=backtrace grade
```

![image](https://user-images.githubusercontent.com/104156058/230925899-8a081d71-2d44-4c21-af7f-aeca1ab174a7.png)

## Alarm ([hard](https://pdos.csail.mit.edu/6.S081/2020/labs/guidance.html))

Timer interrupt를 이용하여 일정 시간(단위: tick)마다 콜백 함수를 호출하는 `sigalarm` 시스템 콜을 구현하는 실습입니다.

### Add alarmtest.c to makefile

테스트 파일인 `alarmtest.c`가 컴파일되도록 `Makefile`에 추가합니다.

```makefile
UPROGS=\
...
	$U/_alarmtest\
```

### Add sigalarm/sigreturn system call

`alarmtest.c`에서 사용되는 `sigalarm`과 `sigreturn` 시스템 콜을 구현해야 합니다.

`syscall.h`에 시스템 콜 번호를 추가합니다.

```c
// System call numbers
...
#define SYS_sigalarm 22
#define SYS_sigreturn 23
```

`syscall.c`에 시스템 콜을 추가합니다.

```c
...
extern uint64 sys_sigalarm(void);
extern uint64 sys_sigreturn(void);
```

```c
static uint64 (*syscalls[])(void) = {
...
    [SYS_sigalarm] sys_sigalarm,
    [SYS_sigreturn] sys_sigreturn
};
```

`alarmtest.c`에서 시스템 콜을 사용할 수 있도록 `user/user.h`와 `user/usys.pl`에 추가합니다.

```c
// system calls
...
int sigalarm(int, void (*)());
int sigreturn(void);
```

```perl
#!/usr/bin/perl -w

# Generate usys.S, the stubs for syscalls.

...
entry("sigalarm");
entry("sigreturn");
```

### Implement sigalarm system call

`struct proc` 구조체에 `sigalarm` 구현에 필요한 필드들을 추가합니다.

```c
// Per-process state
struct proc {
...
    // for sigalarm
    int alarm_interval;      // alarm interval
    int tick_passed;         // how many ticks have passed since last call
    void (*alarm_handler)(); // handler function
};
```

추가한 필드들은 `allocproc()`에서 초기화합니다.

```c
static struct proc *allocproc(void) {
...
    // for sigalarm
    p->alarm_interval = 0;
    p->tick_passed = 0;
    p->alarm_handler = 0;

    return p;
}
```

`sysproc.c`에 `sys_sigalarm()`과 `sys_sigreturn()`을 정의합니다. `sys_sigreturn()`은 일단 비워둡니다.

```c
uint64 sys_sigalarm(void) {
    int ticks;         // alarm interval
    void (*handler)(); // handler function

    if (argint(0, &ticks) < 0)
        return -1;
    if (argaddr(1, (uint64 *)&handler) < 0)
        return -1;

    struct proc *p = myproc();
    p->alarm_interval = ticks;
    p->alarm_handler = handler;

    return 0;
}

uint64 sys_sigreturn(void) { return 0; }

```

`usertrap()`에서 `which_dev`가 2인 경우(timer interrupt), `yield()` 대신 `p->tick_passed`를 1 증가시키고 `p->alarm_interval`과 같으면 `p->alarm_handler`로 점프하는 코드를 추가합니다.

```c
void usertrap(void) {
...
    // give up the CPU if this is a timer interrupt.
    if (which_dev == 2) {
        // yield();
        if (++p->tick_passed == p->alarm_interval) {
            p->trapframe->epc = (uint64)p->alarm_handler;
            p->tick_passed = 0;
        }
    }

    usertrapret();
}
```

### Test

```bash
make GRADEFLAGS=alarmtest grade
```

![image](https://user-images.githubusercontent.com/104156058/236628677-4742d7b2-4939-4359-a13f-e0b0dbf8b7a9.png)

아직 `sigreturn` 시스템 콜을 구현하지 않아서 test1과 test2를 통과하지 못합니다.

### Implement sigreturn system call

`sigreturn` 시스템 콜은 `sigalarm`의 handler function으로 점프한 이후 다시 원래 코드로 돌아오는 역할을 합니다.

`struct proc` 구조체에 기존의 trapframe을 저장할 backup trapframe 필드를 추가합니다.

```c
struct proc {
...
    // for sigreturn
    struct trapframe *alarm_trapframe; // backup trapframe
};
```

`allocproc()`에서 backup trapframe의 메모리를 할당합니다.

```c
static struct proc *allocproc(void) {
...
    // for sigreturn
    if ((p->alarm_trapframe = kalloc()) == 0) {
        release(&p->lock);
        return 0;
    }

    return p;
}
```

`freeproc()`에서 backup trapframe의 메모리를 해제합니다.

```c
static void freeproc(struct proc *p) {
...
    // for sigreturn
    if (p->alarm_trapframe) {
        kfree(p->alarm_trapframe);
        p->alarm_trapframe = 0;
    }
}
```

`usertrap()`에서 timer interrupt를 처리할 때 기존의 trapframe을 backup하는 코드를 추가합니다.

```c
void usertrap(void) {
...
    // give up the CPU if this is a timer interrupt.
    if (which_dev == 2) {
        // yield();
        if (++p->tick_passed == p->alarm_interval) {
            *p->alarm_trapframe = *p->trapframe; // backup trapframe
            p->trapframe->epc = (uint64)p->alarm_handler;
            p->tick_passed = 0;
        }
    }

    usertrapret();
}
```

`sysproc.c`에 `sys_sigreturn()`을 구현합니다. Backup trapframe을 다시 복구시킵니다.

```c
uint64 sys_sigreturn(void) {
    struct proc *p = myproc();

    *p->trapframe = *p->alarm_trapframe; // restore trapframe

    return 0;
}
```

### Test

```bash
make GRADEFLAGS=alarmtest grade
```

![image](https://user-images.githubusercontent.com/104156058/236629401-fe37a7c5-8402-47eb-86fc-e28fdf5e6f7c.png)
