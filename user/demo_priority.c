#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void busy_loop(int count, const char *name) {
  for (int i = 0; i < count; i++) {
    if (i % (count / 10) == 0) {
      printf("%s progress: %d%%\n", name, (i * 100) / count);
    }
  }
}

int main() {
  int pid = fork();
  if (pid == 0) {
    // 子进程
    printf("Child process setting priority to 5\n");
    setpriority(5); // 设置较高优先级
    busy_loop(100000000, "Child");
    exit(0);
  } else {
    // 父进程
    printf("Parent process setting priority to 15\n");
    setpriority(15); // 设置较低优先级
    busy_loop(100000000, "Parent");
    wait(0);
    exit(0);
  }
}