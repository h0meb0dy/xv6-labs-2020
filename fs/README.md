# [MIT 6.S081 Fall 2020] Lab: file system

> https://pdos.csail.mit.edu/6.S081/2020/labs/fs.html

## Large files

`struct dinode`의 `addrs`에서 원래 direct block 12개, indirect block 1개였던 것을 direct block 11개, indirect block 2개로 바꿔야 하므로, `NDIRECT`, `struct dinode`, `struct inode`의 정의를 수정합니다.

```c
/* kernel/fs.h */

// #define NDIRECT 12
#define NDIRECT 11
...

// On-disk inode structure
struct dinode {
...
    // uint addrs[NDIRECT + 1]; // Data block addresses
    uint addrs[NDIRECT + 2];
};
```

```c
/* kernel/file.h */

// in-memory copy of an inode
struct inode {
...
    // uint addrs[NDIRECT + 1];
    uint addrs[NDIRECT + 2];
};
```

`NDIRECT`는 1 감소하였고, doubly-indirect block을 만들어서 `NINDIRECT * NINDIRECT`만큼 block을 더 할당할 수 있기 때문에, 이에 맞게 `fs.h`에서 `MAXFILE`의 정의를 수정합니다.

```c
/* kernel/fs.h */

// #define MAXFILE (NDIRECT + NINDIRECT)
#define MAXFILE (NDIRECT + NINDIRECT + NINDIRECT * NINDIRECT)
```

`bmap()`에 doubly-indirect block을 할당하는 코드를 추가합니다.

```c
/* kernel/fs.c */

static uint
bmap(struct inode *ip, uint bn) {
...
    /* map doubly-indirect block */
    /* bn == NINDIRECT * single_idx + double_idx */
    uint single_idx = bn / NINDIRECT;
    uint double_idx = bn % NINDIRECT;

    if (single_idx < NINDIRECT) {
        if ((addr = ip->addrs[NDIRECT + 1]) == 0) {
            ip->addrs[NDIRECT + 1] = addr = balloc(ip->dev);
        }

        bp = bread(ip->dev, addr);
        a = (uint *)bp->data;
        if ((addr = a[single_idx]) == 0) {
            a[single_idx] = addr = balloc(ip->dev);
            log_write(bp);
        }
        brelse(bp);

        bp = bread(ip->dev, addr);
        a = (uint *)bp->data;
        if ((addr = a[double_idx]) == 0) {
            a[double_idx] = addr = balloc(ip->dev);
            log_write(bp);
        }
        brelse(bp);

        return addr;
    }

    panic("bmap: out of range");
}
```

![image](https://github.com/h0meb0dy/h0meb0dy/assets/104156058/d0286767-983e-4796-bb8d-c3022b70a286)

![image](https://github.com/h0meb0dy/h0meb0dy/assets/104156058/64dcf98e-cfc1-41a6-a734-cee7e7bdec3c)

## Symbolic links

`symlinktest`를 `Makefile`에 추가합니다.

```makefile
# Makefile

UPROGS=\
...
	$U/_symlinktest\
```

`symlink` system call에 필요한 선언과 정의들을 추가합니다.

```c
/* kernel/syscall.h */

// System call numbers
...
#define SYS_symlink 22
```

```c
/* kernel/syscall.c */

...
extern uint64 sys_symlink(void);

static uint64 (*syscalls[])(void) = {
...
    [SYS_symlink] sys_symlink,
};
```

```c
/* user/user.h */

// system calls
...
int symlink(char *, char *);
```

```perl
# user/usys.pl

entry("fork");
...
entry("symlink");
```

필요한 flag들도 정의합니다.

```c
/* kernel/stat.h */

#define T_DIR 1    // Directory
...
#define T_SYMLINK 4 // Symbolic link
```

```c
/* kernel/fcntl.h */

#define O_RDONLY 0x000
...
#define O_NOFOLLOW 0x800
```

Symbolic link 파일이 원본 파일의 이름을 참조해서 따라갈 수 있도록 `struct inode`에 원본 파일의 이름을 저장하는 `symlink_target`을 추가합니다.

```c
/* kernel/file.h */

#include "param.h"
...
// in-memory copy of an inode
struct inode {
...
    char symlink_target[MAXPATH + 1];
};
```

`symlink` system call의 구현체인 `sys_symlink()` 함수를 정의합니다. Target file name과 symbolic link file name을 인자로 받아서 symbolic link file을 생성하고 inode의 `symlink_target`에 target file name을 저장합니다.

```c
/* kernel/sysfile.c */

uint64 sys_symlink(void) {
    char target[MAXPATH];     // target file path
    char symlink[MAXPATH];    // symbolic link file path
    struct inode *symlink_ip; // symbolic link file inode

    /* fetch arguments */
    if (argstr(0, target, MAXPATH) < 0 || argstr(1, symlink, MAXPATH) < 0) {
        return -1;
    }

    begin_op();

    /* create symbolic link */
    if (!(symlink_ip = create(symlink, T_SYMLINK, 0, 0))) {
        end_op();
        return -1;
    }

    memmove(symlink_ip->symlink_target, target, MAXPATH);

    iunlockput(symlink_ip);
    end_op();

    return 0;
}
```

`sys_open()`에 symbolic link file을 처리하는 코드를 추가합니다. `type`이 `T_SYMLINK`인 파일을 인자로 받고 `O_NOFOLLOW` 옵션이 걸려 있지 않은 경우, `symlink_target`을 참조하여 원본 파일의 inode를 가져옵니다. 이때 symbolic link끼리 cycle을 형성하고 있는 경우가 있을 수 있으므로 `depth`가 10이 되면 error를 반환합니다.

```c
/* kernel/sysfile.c */

uint64
sys_open(void) {
...
    /* handle symbolic link */
    int depth = 0;
    while (ip->type == T_SYMLINK && !(omode & O_NOFOLLOW)) {
        if (depth++ >= 10) {
            /* symbolic link cycle */
            iunlockput(ip);
            end_op();
            return -1;
        }
        iunlockput(ip);
        if(!(ip = namei(ip->symlink_target))) {
            /* target doesn't exist */
            end_op();
            return -1;
        }
        ilock(ip);
    }

    if (ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)) {
...
}
```

![image](https://github.com/h0meb0dy/h0meb0dy/assets/104156058/a76a943c-8999-4d6e-abfb-0c2ee4f7daf0)

![image](https://github.com/h0meb0dy/h0meb0dy/assets/104156058/089eda06-f864-461a-886c-a56629f50068)
