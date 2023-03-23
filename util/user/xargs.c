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
