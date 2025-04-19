#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define SHM_KEY 1234
#define SHM_SIZE 4096
#define IPC_CREAT 0x0100 

void test_shared_memory() {
  int shmid;
  uint64 addr_int;
  char *addr;

  // 创建或获取共享内存段
  shmid = shmget(SHM_KEY, SHM_SIZE, IPC_CREAT);
  if (shmid < 0) {
    printf("shmget failed\n");
    exit(1);
  }
  printf("shmget succeeded, shmid: %d\n", shmid);

  // 映射共享内存段
  addr_int = shmat(shmid, 0);
  if (addr_int == (uint64)-1) {
    printf("shmat failed\n");
    exit(1);
  }
  addr = (char *)addr_int;
  printf("shmat succeeded, addr: %p\n", addr);

  // 写入共享内存
  strcpy(addr, "Hello, Shared Memory!");
  printf("Written to shared memory: %s\n", addr);

  // 在另一个进程中验证共享内存
  if (fork() == 0) {
    uint64 child_addr_int = shmat(shmid, 0);
    if (child_addr_int == (uint64)-1) {
      printf("Child: shmat failed\n");
      exit(1);
    }
    char *child_addr = (char *)child_addr_int;
    printf("Child: shmat succeeded, addr: %p\n", child_addr);
    printf("Child: Read from shared memory: %s\n", child_addr);

    // 解除映射
    if (shmdt((uint64)child_addr) < 0) {
      printf("Child: shmdt failed\n");
      exit(1);
    }
    printf("Child: shmdt succeeded\n");
    exit(0);
  }

  wait(0);

  // 解除映射
  if (shmdt((uint64)addr) < 0) {
    printf("shmdt failed\n");
    exit(1);
  }
  printf("shmdt succeeded\n");

  // 释放共享内存段
  if (shmrel(shmid) < 0) {
    printf("shmrel failed\n");
    exit(1);
  }
  printf("shmrel succeeded\n");
}

int main(int argc, char *argv[]) {
  test_shared_memory();
  exit(0);
}