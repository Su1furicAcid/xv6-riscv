#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void
print(const char *s)
{
  write(1, s, strlen(s));
}

int
main(void)
{
  int initial_procnum = getprocnum();
  printf("Initial number of processes: %d\n", initial_procnum);

  int pid;
  for (int i = 0; i < 5; i++) {
    pid = fork();
    if (pid < 0) {
      printf("fork failed\n");
      exit(1);
    } else if (pid == 0) {
      sleep(10);
      exit(0);
    } else {
      int current_procnum = getprocnum();
      printf("Number of processes after fork %d: %d\n", i + 1, current_procnum);
    }
  }

  while (wait(0) > 0);

  int final_procnum = getprocnum();
  printf("Final number of processes: %d\n", final_procnum);

  exit(0);
}