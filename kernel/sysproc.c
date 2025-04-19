#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_mysbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(mygrowproc(n) < 0)
    return -1;
  return addr;
}

void
sys_demo_allocator(void)
{
  demo_allocator();
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if(n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// 获取进程数量
uint64
sys_getprocnum(void)
{
  // TODO: 实现获取进程数量的系统调用
  return getprocnum();
}

// 设置进程优先级
void
sys_setpriority(void)
{
  int priority;
  argint(0, &priority);
  int pid;
  argint(1, &pid);
  setpriority(priority, pid);
}

// 获取指定进程的优先级
uint64
sys_getpriority(void)
{
  int pid;
  argint(0, &pid);
  return getpriority(pid);
}

uint64
sys_shmget(void)
{
  int key;
  int size;
  int flags;
  argint(0, &key);
  argint(1, &size);
  argint(2, &flags);
  return shmget(key, size, flags);
}

uint64
sys_shmat(void)
{
  int shmid;
  uint64 addr;
  argint(0, &shmid);
  argaddr(1, &addr);
  return shmat(shmid, addr);
}

uint64
sys_shmdt(void)
{
  uint64 addr;
  argaddr(0, &addr);
  return shmdt(addr);
}

uint64
sys_shmrel(void)
{
  int shmid;
  argint(0, &shmid);
  return shmrel(shmid);
}
