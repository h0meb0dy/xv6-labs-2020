# [MIT 6.S081 Fall 2020] Lab: Multithreading

> https://pdos.csail.mit.edu/6.S081/2020/labs/thread.html

## Uthread: switching between threads

`thread` 구조체에 레지스터를 저장할 공간과 thread가 참조할 함수 포인터를 추가한다.

```c
/* user/uthread.c */

struct reg {
    uint64 ra;
    uint64 sp;
    uint64 s0;
    uint64 s1;
    uint64 s2;
    uint64 s3;
    uint64 s4;
    uint64 s5;
    uint64 s6;
    uint64 s7;
    uint64 s8;
    uint64 s9;
    uint64 s10;
    uint64 s11;
};

struct thread {
    struct reg reg;         /* the thread's registers */
    char stack[STACK_SIZE]; /* the thread's stack */
    int state;              /* FREE, RUNNING, RUNNABLE */
    void (*func)();         /* thread function */
};
```

`thread_schedule()`에서 `thread_switch()`를 호출한다.

```c
/* user/uthread.c */

void thread_schedule(void) {
...
    if (current_thread != next_thread) { /* switch threads?  */
...
        /* YOUR CODE HERE
         * Invoke thread_switch to switch from t to next_thread:
         * thread_switch(??, ??);
         */
        thread_switch((uint64)t, (uint64)next_thread);
    } else
        next_thread = 0;
}
```

`swtch.S`의 `swtch()` 구현을 참고하여 `uthread_switch.S`의 `thread_switch()`를 구현한다. 현재 thread의 callee-saved register들과 program counter, stack pointer를 `thread` 구조체의 `reg`에 저장하고, 다음 thread의 그 값들을 레지스터로 load한다. 

```assembly
/* user/uthread_switch.S */

	.text

	/*
         * save the old thread's registers,
         * restore the new thread's registers.
         */

	.globl thread_switch
thread_switch:
	/* YOUR CODE HERE */

    /* save registers of current thread */
    sd ra, 0(a0)
    sd sp, 8(a0)
    sd s0, 16(a0)
    sd s1, 24(a0)
    sd s2, 32(a0)
    sd s3, 40(a0)
    sd s4, 48(a0)
    sd s5, 56(a0)
    sd s6, 64(a0)
    sd s7, 72(a0)
    sd s8, 80(a0)
    sd s9, 88(a0)
    sd s10, 96(a0)
    sd s11, 104(a0)

    /* load registers of next thread */
    ld ra, 0(a1)
    ld sp, 8(a1)
    ld s0, 16(a1)
    ld s1, 24(a1)
    ld s2, 32(a1)
    ld s3, 40(a1)
    ld s4, 48(a1)
    ld s5, 56(a1)
    ld s6, 64(a1)
    ld s7, 72(a1)
    ld s8, 80(a1)
    ld s9, 88(a1)
    ld s10, 96(a1)
    ld s11, 104(a1)

	ret    /* return to ra */
```

새로운 thread를 생성하는 `thread_create()`를 구현한다.

```c
/* user/uthread.c */

void thread_create(void (*func)()) {
...
    // YOUR CODE HERE
    t->reg.ra = (uint64)func;
    t->reg.sp = (uint64)&t->state;
}
```

![image](https://github.com/h0meb0dy/h0meb0dy/assets/104156058/5fc899b8-8c95-4a8e-8d14-1f4a44eaecfc)

![image](https://github.com/h0meb0dy/h0meb0dy/assets/104156058/1dab667c-6050-413e-9415-eb92959c34e0)

## Using threads

`put_thread()` 앞쪽에 `lock`을 선언한다.

```c
/* notxv6/ph.c */

pthread_mutex_t lock;

static void *
put_thread(void *xa) {
```

`main()`에서 `pthread_create()` 호출 전에 `lock`을 초기화한다.

```c
/* notxv6/ph.c */

    pthread_mutex_init(&lock, NULL);

    //
    // first the puts
    //
```

`put_thread()`에서 `put()`을 lock과 unlock으로 감싼다.

```c
/* notxv6/ph.c */

    for (int i = 0; i < b; i++) {
        pthread_mutex_lock(&lock);
        put(keys[b * n + i], n);
        pthread_mutex_unlock(&lock);
    }
```

![image](https://github.com/h0meb0dy/h0meb0dy/assets/104156058/c18aec78-68bc-41f7-a8a5-da9fcf504520)

위처럼 `put()` 전체에 lock이 걸리면 single thread와 다를 바가 없기 때문에 `puts()` 내부의 critical section에만 lock이 걸리도록 하여 속도를 증진시킨다.

`lock` 선언을 `puts()` 앞쪽으로 옮긴다.

```c
/* notxv6/ph.c */

pthread_mutex_t lock;

static void put(int key, int value) {
```

위에서 `puts()`를 감싼 lock과 unlock을 제거하고, `puts()` 내부에서 `key`를 새로 추가하는 `insert()`를 lock과 unlock으로 감싼다.

```c
/* notxv6/ph.c */

    } else {
        // the new is new.
        pthread_mutex_lock(&lock);
        insert(key, value, &table[i], table[i]);
        pthread_mutex_unlock(&lock);
    }
```

![image](https://github.com/h0meb0dy/h0meb0dy/assets/104156058/9be70f6b-f7d5-4bbd-9e3b-211770ddc2ab)

## Barrier

각각의 thread들이 `barrier()`에 도달할 때마다 `round`를 1씩 증가시키고 그 thread를 재운다. `round`가 thread 수와 같아지면 `round`를 초기화하고 `bstate.round`를 1 증가시킨 후 모든 thread를 깨운다. `barrier()`의 시작과 끝은 lock과 unlock으로 감싼다.

```c
/* notxv6/barrier.c */

static void
barrier() {
    pthread_mutex_lock(&bstate.barrier_mutex);
    // YOUR CODE HERE
    //
    // Block until all threads have called barrier() and
    // then increment bstate.round.
    //
    round++;
    if (round == nthread) {
        round = 0;
        bstate.round++;
        pthread_cond_broadcast(&bstate.barrier_cond);
    } else {
        pthread_cond_wait(&bstate.barrier_cond, &bstate.barrier_mutex);
    }
    pthread_mutex_unlock(&bstate.barrier_mutex);
}
```

![image](https://github.com/h0meb0dy/h0meb0dy/assets/104156058/9d50e311-adc1-4460-a56f-8672b63dfd7d)
