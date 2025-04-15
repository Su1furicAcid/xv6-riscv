#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: demo_change_priority <pid> <priority>\n");
        exit(1);
    }

    int pid = atoi(argv[1]);       // 获取目标进程的 PID
    int priority = atoi(argv[2]); // 获取新的优先级

    if (priority < 0) {
        printf("Error: Priority must be a non-negative integer.\n");
        exit(1);
    }

    // 调用系统调用设置优先级
    setpriority(priority, pid);

    printf("Changed priority of process %d to %d\n", pid, priority);

    exit(0);
}