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
