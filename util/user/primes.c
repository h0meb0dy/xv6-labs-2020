#include "kernel/types.h"
#include "user/user.h"

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
