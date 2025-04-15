#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main() {
  int pid;
  uint64 shm_addr;

  // 父进程创建共享内存页
  shm_addr = shm_create(4096);
  if (shm_addr == -1) {
    printf("Failed to create shared memory\n");
    exit(1);
  }
  printf("Parent created shared memory at address: 0x%p\n", (void *)shm_addr);

  pid = fork();
  if (pid == 0) {
    // 子进程
    printf("Child attaching to shared memory at address: 0x%p\n", (void *)shm_addr);
    if (shm_attach(shm_addr) == -1) {
      printf("Child failed to attach to shared memory\n");
      exit(1);
    }

    char *shared_data = (char *)shm_addr;
    printf("Child writing to shared memory\n");
    shared_data[0] = 'H';
    shared_data[1] = 'i';
    shared_data[2] = '\0';

    printf("Child exiting\n");
    exit(0);
  } else {
    // 父进程
    wait(0);
    char *shared_data = (char *)shm_addr;
    printf("Parent reading from shared memory: %s\n", shared_data);
    exit(0);
  }
}