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
