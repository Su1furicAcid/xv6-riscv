#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main() {
    int pid = fork();

    if (pid < 0) {
        printf("Fork failed\n");
        exit(1);
    } else if (pid == 0) {
        // 子进程
        int counter = 0;
        printf("Child process started (PID: %d)\n", getpid());

        while (1) {
            counter++;
            if (counter % 1000000 == 0) {
                counter = 0;
            }
        }
        exit(0);
    } else {

    }

    exit(0);
}