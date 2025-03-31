#include "kernel/types.h"
#include "kernel/stat.h"
#include "user.h"

int main() {
  printf("Starting memory demo from user program...\n");
  memory_demo(); // 调用系统调用
  printf("Memory demo finished.\n");
  exit(0);
}