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

// 创建共享内存页
uint64
sys_shm_create(void)
{
  struct proc *p = myproc();
  uint64 size;

  argaddr(0, &size);

  if (size > PGSIZE) // 限制共享页大小为 1 页
    return -1;

  for (int i = 0; i < MAX_SHARED_PAGES; i++) {
    if (p->shared_pages[i].pa == 0) {
      char *pa = kalloc(); // 分配物理页
      if (pa == 0)
        return -1;

      memset(pa, 0, PGSIZE);
      p->shared_pages[i].pa = (uint64)pa;
      p->shared_pages[i].va = TRAPFRAME - (i + 1) * PGSIZE; // 映射到 trapframe 下方
      p->shared_pages[i].ref_count = 1;

      if (mappages(p->pagetable, p->shared_pages[i].va, PGSIZE, (uint64)pa, PTE_R | PTE_W | PTE_U) < 0) {
        kfree(pa);
        p->shared_pages[i].pa = 0;
        p->shared_pages[i].va = 0;
        p->shared_pages[i].ref_count = 0;
        return -1;
      }

      return p->shared_pages[i].va; // 返回虚拟地址
    }
  }

  return -1; // 没有空闲的共享页槽位
}

// 附加共享内存页
uint64
sys_shm_attach(void)
{
  struct proc *p = myproc();
  uint64 va;
  argaddr(0, &va);

  for (int i = 0; i < MAX_SHARED_PAGES; i++) {
    if (p->shared_pages[i].pa != 0 && p->shared_pages[i].va == va) {
      p->shared_pages[i].ref_count++;
      return va;
    }
  }

  return -1; // 没有找到对应的共享页
}

// 释放给定的共享内存页
void
sys_shm_release(uint64 va)
{
  struct proc *p = myproc();

  for (int i = 0; i < MAX_SHARED_PAGES; i++) {
    if (p->shared_pages[i].va == va) {
      if (--p->shared_pages[i].ref_count == 0) {
        uvmunmap(p->pagetable, p->shared_pages[i].va, 1, 1);
        kfree((void *)p->shared_pages[i].pa);
      }
      p->shared_pages[i].pa = 0;
      p->shared_pages[i].va = 0;
      p->shared_pages[i].ref_count = 0;
      break;
    }
  }
}