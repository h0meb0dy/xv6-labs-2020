# [MIT 6.S081 Fall 2020] Lab: Xv6 and Unix utilities

> https://pdos.csail.mit.edu/6.S081/2020/labs/util.html

## Boot xv6 ([easy](https://pdos.csail.mit.edu/6.S081/2020/labs/guidance.html))

xv6를 부팅하고 앞으로의 실습에 필요한 환경을 구축하는 실습이다.

### Install dependencies

빌드에 필요한 dependency들을 설치한다.

> https://pdos.csail.mit.edu/6.S081/2020/tools.html

위 링크에서 Installing via APT (Debian/Ubuntu) 부분을 따라한다. 다음의 과정은 Ubuntu 20.04 버전에서 정상적으로 수행할 수 있다.

```bash
sudo apt install -y git build-essential gdb-multiarch qemu-system-misc gcc-riscv64-linux-gnu binutils-riscv64-linux-gnu 
sudo apt remove -y qemu-system-misc
sudo apt install -y qemu-system-misc=1:4.2-3ubuntu6
```

나중에 채점 스크립트를 실행하기 위해 `python`(python 2)도 필요하다.

```bash
sudo apt install -y python
```

### Clone xv6

Xv6 repository에는 각각의 실습들의 기초 코드가 branch의 형태로 저장되어 있다. Repository를 clone하여 이번 실습에 해당하는 `util` branch로 전환한다.

```bash
git clone git://g.csail.mit.edu/xv6-labs-2020
cd xv6-labs-2020/
git checkout util
```

### Build xv6

빌드가 잘 되는지 테스트해본다.

```bash
make qemu
```

![image](https://user-images.githubusercontent.com/104156058/226910910-c5fe5895-cf10-4979-b9bf-fef212b0581c.png)

QEMU를 종료하려면 `ctrl+a`를 눌렀다가 뗀 후 `x`를 누른다.

## sleep ([easy](https://pdos.csail.mit.edu/6.S081/2020/labs/guidance.html))

`sleep` system call을 사용해보는 간단한 실습이다. 시간(단위: 초)을 매개변수로 받아서 그만큼 sleep하는 프로그램을 `sleep.c`에 구현한다.

### Add sleep to Makefile

`make`할 때 `sleep`이 컴파일되도록 `Makefile`에 추가한다.

```makefile
...
UPROGS=\
...
	$U/_sleep\
...
```

### Implement sleep.c

`user` 디렉토리에 `sleep.c` 파일을 추가하고 구현한다.

```c
#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
    // check arguments
    if (argc != 2) {
        fprintf(2, "[-] usage: sleep [seconds]\n");
        exit(1);
    }

    int time = atoi(argv[1]);  // time to sleep

    sleep(time);  // sleep

    return 0;
}
```

### Test

```bash
make GRADEFLAGS=sleep grade
```

![image](https://user-images.githubusercontent.com/104156058/226910758-db918d4c-a3fe-4d46-8a07-03c90a64675f.png)

## pingpong ([easy](https://pdos.csail.mit.edu/6.S081/2020/labs/guidance.html))

`pipe` system call을 이용하여 두 프로세스를 pipe로 연결하고 그 pipe를 통해 byte를 주고받는 프로그램을 구현하는 실습이다.

### Add pingpong to Makefile

`make`할 때 `pingpong`이 컴파일되도록 `Makefile`에 추가한다.

```makefile
...
UPROGS=\
...
	$U/_sleep\
	$U/_pingpong\
...
```

### Implement pingpong.c

`user` 디렉토리에 `pingpong.c` 파일을 추가하고 구현한다.

```c
#include "kernel/types.h"
#include "user/user.h"

int main() {
    int p[2];  // pipe ends

    // create pipe
    if (pipe(p) < 0) {
        fprintf(2, "[-] pipe() failed\n");
        exit(1);
    }

    int pid = fork();
    if (!pid) {
        // child

        char byte = 0;  // byte to ping-pong

        // read from pipe
        if (read(p[0], &byte, 1) == 1)
            printf("%d: received ping\n", getpid());
        else {
            fprintf(2, "[-] ping failed\n");
            exit(1);
        }

        write(p[1], &byte, 1);  // write to pipe

        exit(0);
    } else {
        // parent

        char byte = 'A';  // byte to ping-pong

        write(p[1], &byte, 1);  // write to pipe

        wait(0);  // wait for child

        // read to pipe
        if (read(p[0], &byte, 1) == 1)
            printf("%d: received pong\n", pid);
        else {
            fprintf(2, "[-] pong failed\n");
            exit(1);
        }
    }

    exit(0);
}
```

### Test

```bash
make GRADEFLAGS=pingpong grade
```

![image](https://user-images.githubusercontent.com/104156058/226918386-6977c450-1c60-45d3-a2ea-f0a9d2eb199e.png)

## primes ([moderate](https://pdos.csail.mit.edu/6.S081/2020/labs/guidance.html))/([hard](https://pdos.csail.mit.edu/6.S081/2020/labs/guidance.html))

> https://swtch.com/~rsc/thread/

위 페이지의 소수 판별 체를 구현하는 실습이다. 2 이상 35 이하의 정수들 중 소수만 출력해야 한다.

![img](https://swtch.com/~rsc/thread/sieve.gif)

대략적인 원리를 나타낸 그림이다. 첫 번째 프로세스는 2는 소수이므로 출력하고 2의 배수는 모두 버린 후 남은 정수들을 자식 프로세스로 넘겨준다. 자식 프로세스는 3은 소수이므로 출력하고 3의 배수는 모두 버린 후 남은 정수들을 다시 자식 프로세스로 넘겨준다. 이 과정을 반복하여 자식 프로세스로 넘겨줄 정수가 하나도 남지 않으면 종료한다.

첫 번째 자식 프로세스(3의 배수를 버리는 단계)부터 마지막 프로세스까지는 재귀적으로 처리할 수 있지만, 최초 프로세스(2의 배수를 버리는 단계)는 부모 프로세스가 없기 때문에, 원리는 같지만 구현이 약간 달라진다.

### Add primes to Makefile

`make`할 때 `primes`가 컴파일되도록 `Makefile`에 추가한다.

```makefile
...
UPROGS=\
...
	$U/_sleep\
	$U/_pingpong\
	$U/_primes\
...
```

### Implement primesieve() (recursive function)

첫 번째 자식 프로세스부터 마지막 프로세스까지에 해당하는 `primesieve()` 함수를 구현한다.

`primesieve()` 함수는 부모 프로세스로부터 파이프를 통해 정수들을 입력받는다. 가장 작은 정수는 소수일 것이므로 출력하고, 그 정수의 배수들을 제외한 나머지를 파이프를 통해 자식 프로세스로 넘겨준다. 그리고 자식 프로세스에서 자기 자신을 재귀적으로 호출한다.

```c
#define MAX 35
int p[2];  // pipe ends

void primesieve() {
    int integers[MAX];  // buffer for integers
    int prime = 0;      // prime number to print (smallest integer)

    // read integers from pipe
    if (read(p[0], integers, sizeof(int) * MAX) != sizeof(int) * MAX) {
        fprintf(2, "[-] read() failed\n");
        exit(1);
    }

    // print prime
    for (int i = 0; i < MAX; i++) {
        if (integers[i]) {
            prime = integers[i];
            break;
        }
    }
    if (prime)
        printf("prime %d\n", prime);
    else
        exit(0);  // no remaining integer

    // write remaining integers to pipe
    for (int i = 0; i < MAX; i++) {
        if (!integers[i]) continue;                   // dropped integer
        if (!(integers[i] % prime)) integers[i] = 0;  // drop non-prime
    }
    int pid = fork();
    if (!pid) {
        // child
        primesieve();  // recursive call
    } else {
        // parent
        write(p[1], integers, sizeof(int) * MAX);
        wait(0);
    }
}
```

### Implement main()

`main()` 함수는 2부터 35까지의 정수 중, 2를 출력하고 2의 배수들을 버린 후 나머지를 자식 프로세스로 넘겨준다. 자식 프로세스는 `primesieve()`를 호출한다.

```c
int main() {
    int integers[MAX];  // buffer for integers

    // initialize buffer (1 is not prime)
    for (int i = 1; i < MAX; i++) {
        integers[i] = i + 1;
    }

    // create pipe
    if (pipe(p) < 0) {
        fprintf(2, "[-] pipe() failed\n");
        exit(1);
    }

    // print 2 (first prime)
    int prime = 2;
    printf("prime %d\n", prime);

    // write remaining integers to pipe
    for (int i = 0; i < MAX; i++) {
        if (!integers[i]) continue;                   // dropped integer
        if (!(integers[i] % prime)) integers[i] = 0;  // drop non-prime
    }
    int pid = fork();
    if (!pid) {
        // child
        primesieve();
    } else {
        // parent
        write(p[1], integers, sizeof(int) * MAX);
        wait(0);
    }

    exit(0);
}
```

### Test

```bash
make GRADEFLAGS=primes grade
```

![image](https://user-images.githubusercontent.com/104156058/226946922-b634b031-b038-41e8-9494-86c229604538.png)

## find ([moderate](https://pdos.csail.mit.edu/6.S081/2020/labs/guidance.html))

문자열을 인자로 받아서, 그 문자열이 경로명에 포함된 파일들을 찾는 `find` 프로그램을 구현하는 실습이다.

### Add find to Makefile

`make`할 때 `find`가 컴파일되도록 `Makefile`에 추가한다.

```makefile
UPROGS=\
...
	$U/_sleep\
	$U/_pingpong\
	$U/_primes\
	$U/_find\
```

### Implement find()

탐색할 경로 `path`와 특정 문자열 `name`을 인자로 받아서, 그 문자열을 포함하는 파일 경로명을 재귀적으로 모두 찾아서 출력하는 `find()` 함수를 구현한다. `user/ls.c`를 참고하면 어렵지 않게 구현할 수 있다.

먼저 문자열 안에서 문자열을 찾는 `strstr()` 함수를 구현한다. 경로명에 특정 문자열이 있는지 확인할 때 사용된다.

```c
// find substring from string
// if substring exists, return offset of first character
// if not, return -1
int strstr(char *str, char *substr) {
    int str_len = strlen(str);
    int substr_len = strlen(substr);

    int found = 0;
    for (int i = 0; i <= str_len - substr_len; i++) {
        for (int j = 0; j < substr_len; j++) {
            if (str[i + j] != substr[j])
                break;  // not match
            else if (j == substr_len - 1)
                found = 1;  // match
        }
        if (found) return i;
    }

    return -1;
}
```

`find()` 함수는 인자로 받은 `path`의 `stat`을 가져와서 `type`을 확인한다. 디렉토리이면 그 디렉토리 안에 있는 모든 파일에 대해 반복문을 돌며 다시 새로운 `path`로 `find()`를 재귀적으로 호출한다. `type`이 파일이면 `path` 뒤에 파일명을 붙인 문자열에 `name`이 포함되는지 확인하고, 포함되면 출력한다.

몇 가지 주의해야 할 점이 있다. 첫째, xv6에서 동시에 열 수 있는 파일의 최대 개수는 16개(`kernel/param.h`의 `NOFILE`)로 정의되어 있기 때문에, `find()`를 재귀적으로 호출하는 과정에서 더 이상 사용하지 않는 파일은 닫아주어야 한다. 둘째, `.`와 `..`도 하나의 파일로 생각하면 `find()`가 재귀적으로 호출되는 과정에서 무한루프에 빠지게 되므로 제외해주어야 한다.

```c
// find files including specific `name` in `path`
// if `path` is filename, check if it includes `name`
void find(char *path, char *name) {
    int fd = open(path, 0);
    if (fd < 0) {
        fprintf(2, "[-] open() failed\n");
        exit(1);
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        fprintf(2, "[-] fstat() failed\n");
        close(fd);
        exit(1);
    }

    char buf[512] = {0};
    switch (st.type) {
        case T_FILE:
            if (strstr(path, name) >= 0) printf("%s\n", path);
            break;

        case T_DIR:
            if (strlen(path) + 1 + DIRSIZ + 1 > sizeof(buf)) {
                fprintf(2, "[-] directory name is too long\n");
                exit(1);
            }

            strcpy(buf, path);
            char *p = buf + strlen(buf);
            *p++ = '/';

            struct dirent de;
            while (read(fd, &de, sizeof(de)) == sizeof(de)) {
                if (!de.inum) continue;
                if (!strcmp(de.name, ".") || !strcmp(de.name, "..")) continue;  // not recurse into '.' and '..'
                memmove(p, de.name, DIRSIZ);                                    // buf is new path
                p[strlen(de.name)] = 0;
                find(buf, name);  // recursive call
            }
            break;
    }

    close(fd);  // file descriptor buffer is small
}
```

### Implement main()

`main()`에서는 매개변수들이 형식에 맞게 들어왔는지 검사하고, `find()`를 호출하여 탐색을 시작한다.

```c
int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(2, "[-] usage: find [directory] [name]\n");
        exit(1);
    }

    find(argv[1], argv[2]);

    exit(0);
}
```

### Test

```bash
make GRADEFLAGS=find grade
```

![image](https://user-images.githubusercontent.com/104156058/227215188-44433977-1521-42b0-9b75-6e3c3569af78.png)

## xargs ([moderate](https://pdos.csail.mit.edu/6.S081/2020/labs/guidance.html))

파이프라인으로 입력을 받아서 그 입력을 매개변수로 붙여 실행하는 `xargs` 명령어를 구현하는 실습이다.

### Add xargs to Makefile

`make`할 때 `xargs`가 컴파일되도록 `Makefile`에 추가한다.

```makefile
...
UPROGS=\
...
	$U/_sleep\
	$U/_pingpong\
	$U/_primes\
	$U/_find\
	$U/_xargs\
...
```

### Implement xargs.c

앞에서 실행된 명령어의 결과가 파이프라인으로 전달되면, `xargs`에서 표준 입력으로 받을 수 있다. 받은 입력은 새로운 매개변수로 넣은 후 `fork()`와 `exec()`으로 새로 만들어진 명령어를 실행한다.

```c
#include "kernel/types.h"
#include "kernel/param.h"
#include "user/user.h"

#define MAXBUF 100  // maximum length of input buffer

int main(int argc, char *argv[]) {
    char buf[MAXBUF] = {0};

    while (read(0, buf, MAXBUF))  // read from pipe
    {
        /* generate new argv */
        char *new_argv[MAXARG] = {0};
        int arg_idx = 0;
        for (; argv[arg_idx + 1] != 0; arg_idx++) {
            if (arg_idx < argc - 1) {
                new_argv[arg_idx] = argv[arg_idx + 1];
            }
        }

        int buf_idx = 0;
        new_argv[arg_idx] = buf;
        while (buf[buf_idx] != 0) {
            if (buf[buf_idx] == '\n') {
                buf[buf_idx] = 0;
                if (!fork()) {
                    exec(new_argv[0], new_argv);  // execute generated command
                    exit(0);
                }
                wait(0);
                new_argv[arg_idx] = &buf[buf_idx + 1];
            }
            buf_idx++;
        }
    }

    exit(0);
}
```

### Test

```bash
make GRADEFLAGS=xargs grade
```

![image](https://user-images.githubusercontent.com/104156058/227240232-d303cf52-904b-401a-a506-a6eaa9be374c.png)
