#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
  printf("The number of processes is %d\n", getprocnum());
  exit(0);
}