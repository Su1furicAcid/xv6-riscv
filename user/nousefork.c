#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main() {
    for (int i = 0; i < 2; i++) { // 创建两个子进程
        int pid = fork();

        if (pid < 0) {
            exit(1);
        } else if (pid == 0) {
            // 子进程
            int counter = 0;

            while (1) {
                counter++;
                if (counter % 1000000 == 0) {
                    counter = 0;
                }
            }
            exit(0);
        }
    }

    // 父进程
    exit(0);
}