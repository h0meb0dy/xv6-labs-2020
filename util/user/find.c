#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fs.h"
#include "user/user.h"

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

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(2, "[-] usage: find [directory] [name]\n");
        exit(1);
    }

    find(argv[1], argv[2]);

    exit(0);
}
