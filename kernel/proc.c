#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];

// 优先级队列
// 不影响原有 proc 列表 仅仅是 proc 列表排序后的结果 仅在调度器中使用
struct {
  struct spinlock lock;
  struct proc *queue[PRIORITY_LEVELS][NPROC]; // 每个优先级的队列; 1~20 是可用的
  int head[PRIORITY_LEVELS]; // 队列头索引
  int tail[PRIORITY_LEVELS]; // 队列尾索引
} priority_queues;

// 初始化优先级队列
void
init_priority_queues(void)
{
  initlock(&priority_queues.lock, "priority_queues");
  for (int i = 1; i < PRIORITY_LEVELS; i++) {
    priority_queues.head[i] = 0;
    priority_queues.tail[i] = 0;
    for (int j = 0; j < NPROC; j++) {
      priority_queues.queue[i][j] = 0;
    }
  }
}

// 将进程加入优先级队列
void
enqueue(struct proc *p)
{
  int priority = p->priority;
  acquire(&priority_queues.lock);
  int tail = priority_queues.tail[priority];
  priority_queues.queue[priority][tail] = p;
  priority_queues.tail[priority] = (tail + 1) % NPROC;
  // printf("Enqueue process %d with priority %d\n", p->pid, priority);
  release(&priority_queues.lock);
}

// 从优先级队列中取出进程
struct proc*
dequeue(int priority)
{
  acquire(&priority_queues.lock);
  int head = priority_queues.head[priority];
  if (priority_queues.head[priority] == priority_queues.tail[priority]) {
    // 队列为空
    release(&priority_queues.lock);
    return 0;
  }
  struct proc *p = priority_queues.queue[priority][head];
  priority_queues.queue[priority][head] = 0;
  priority_queues.head[priority] = (head + 1) % NPROC;
  release(&priority_queues.lock);
  return p;
}

// 共享内存段信息
struct shm_entry {
  int key;           // 唯一标识符
  uint64 start_pa;   // 起始物理地址
  uint64 end_pa;     // 结束物理地址
  int size;          // 大小（以字节为单位）
  int ref_count;     // 引用计数
  int shmid;          // 共享内存段 ID
  uint64 va;        // 映射的起始虚拟地址
};

struct shm_entry shm_table[MAX_SHARED_SEGMENTS]; // 全局共享内存表
struct spinlock shm_lock;  // 保护共享内存表的锁 

// 初始化共享内存表
void init_shm_table(void) {
  initlock(&shm_lock, "shm_lock");
  for (int i = 0; i < MAX_SHARED_SEGMENTS; i++) {
    shm_table[i].key = -1; // 初始化为无效状态
    shm_table[i].start_pa = 0;
    shm_table[i].end_pa = 0;
    shm_table[i].size = 0;
    shm_table[i].ref_count = 0;
    shm_table[i].shmid = -1; // 初始化为无效状态
  }
}

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int) (p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table.
void
procinit(void)
{
  struct proc *p;
  
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      p->state = UNUSED;
      p->kstack = KSTACK((int) (p - proc));
      p->priority = UNUSED_PRIORITY;
      p->create_time = 0;
      p->ready_time = 0;
      p->run_time = 0;
      p->finish_time = 0;
  }
  init_priority_queues(); // 初始化优先级队列
  init_shm_table(); // 初始化共享内存表
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int
allocpid()
{
  int pid;
  
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;
  p->priority = MID_PRIORITY;
  p->create_time = ticks;

  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
  p->priority = UNUSED_PRIORITY;
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// assembled from ../user/initcode.S
// od -t xC ../user/initcode
uchar initcode[] = {
  0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
  0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
  0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
  0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
  0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
  0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;
  
  // allocate one user page and copy initcode's instructions
  // and data into it.
  uvmfirst(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  p->trapframe->epc = 0;      // user program counter
  p->trapframe->sp = PGSIZE;  // user stack pointer

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;

  enqueue(p); // 将进程加入优先级队列

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// 使用malloc的分配内存的版本
int mygrowproc(int n) {
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if((sz = uvmalloc_malloc(p->pagetable, sz, sz + n, PTE_W)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  np->ready_time = ticks;
  enqueue(np); // 将进程加入优先级队列
  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
exit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);
  
  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;
  p->finish_time = ticks;
  p->priority = UNUSED_PRIORITY;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(pp = proc; pp < &proc[NPROC]; pp++){
      if(pp->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;
        if(pp->state == ZOMBIE){
          // Found one.
          pid = pp->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                  sizeof(pp->xstate)) < 0) {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || killed(p)){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  uint64 start_ticks;

  c->proc = 0;
  for(;;){
    intr_on(); // 开启中断

    // 按优先级从高到低遍历队列
    for (int i = 1; i < PRIORITY_LEVELS; i++) {
      p = dequeue(i);
      if (p) {
        acquire(&p->lock);
        if (p->state == RUNNABLE) {
          // 切换到该进程
          p->state = RUNNING;
          c->proc = p;

          start_ticks = ticks;
          swtch(&c->context, &p->context);
          p->run_time += ticks - start_ticks;

          // 恢复调度器状态
          c->proc = 0;
        }
        release(&p->lock);
        break; // 运行一个进程后退出循环
      }
    }

    // 如果没有可运行的进程，进入低功耗模式
    asm volatile("wfi");
  }
}
// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  enqueue(p); // 将进程加入优先级队列
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  static int first = 1;

  // Still holding p->lock from scheduler.
  release(&myproc()->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    fsinit(ROOTDEV);

    first = 0;
    // ensure other cores see first=0.
    __sync_synchronize();
  }

  usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock);  //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    if(p != myproc()){
      acquire(&p->lock);
      if(p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
        enqueue(p);
      }
      release(&p->lock);
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kill(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

void
setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int
killed(struct proc *p)
{
  int k;
  
  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [USED]      "used",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}

// 获取进程数量
int getprocnum(void) {
  struct proc *p;
  int count = 0;

  acquire(&wait_lock);
  for(p = proc; p < &proc[NPROC]; p++) {
    if(p->state != UNUSED) {
      count++;
    }
  }

  // 输出所有进程的信息
  for(p = proc; p < &proc[NPROC]; p++) {
    if(p->state != UNUSED) {
      printf("pid: %d name: %s state: %s priority: %d run_time: %d \n\n",
             p->pid, p->name, (p->state == RUNNING) ? "RUNNING" : (p->state == SLEEPING) ? "SLEEPING" : "RUNNABLE", p->priority, p->run_time);
    }
  }

  release(&wait_lock);
  return count;
}

void setpriority(int priority, int pid) {
  struct proc *p;

  acquire(&wait_lock);
  for(p = proc; p < &proc[NPROC]; p++) {
    if(p->pid == pid) {
      acquire(&p->lock);
      p->priority = priority;
      release(&p->lock);
      break;
    }
  }
  release(&wait_lock);
}

int getpriority(int pid) {
  struct proc *p;
  int priority = 0;

  acquire(&wait_lock);
  for(p = proc; p < &proc[NPROC]; p++) {
    if(p->pid == pid) {
      priority = p->priority;
      break;
    }
  }
  release(&wait_lock);
  return priority;
}

int
shmget(int key, int size, int flags) {
  acquire(&shm_lock);

  // 检查是否已有共享内存段
  for (int i = 0; i < MAX_SHARED_SEGMENTS; i++) {
    if (shm_table[i].key == key) {
      // 找到已有的共享内存段
      if (flags & IPC_CREAT && size > shm_table[i].size) {
        // 如果需要扩展大小
        uint64 oldpa = shm_table[i].end_pa;
        uint64 newpa = PGROUNDUP(oldpa + (size - shm_table[i].size));
        uint64 a;

        // 分配新的物理页
        for (a = oldpa; a < newpa; a += PGSIZE) {
          char *mem = kalloc();
          if (mem == 0) {
            // 分配失败，释放已分配的页
            for (uint64 b = oldpa; b < a; b += PGSIZE) {
              kfree((void *)b);
            }
            release(&shm_lock);
            return -1;
          }
          memset(mem, 0, PGSIZE); // 清零分配的内存
        }

        shm_table[i].end_pa = newpa;
        shm_table[i].size = size;
      }

      release(&shm_lock);
      return shm_table[i].shmid;
    }
  }

  // 如果没有找到共享内存段且设置了 IPC_CREAT，则创建新的共享内存段
  if (flags & IPC_CREAT) {
    for (int i = 0; i < MAX_SHARED_SEGMENTS; i++) {
      if (shm_table[i].key == -1) {
        shm_table[i].key = key;
        shm_table[i].size = size;
        shm_table[i].ref_count = 1;
        shm_table[i].shmid = i;

        uint64 oldpa = PGROUNDUP((uint64)kalloc()); // 确保页对齐
        if (oldpa == 0) {
          release(&shm_lock);
          return -1;
        }

        uint64 newpa = oldpa + PGROUNDUP(size);
        uint64 a;

        // 分配物理页
        for (a = oldpa; a < newpa; a += PGSIZE) {
          char *mem = kalloc();
          if (mem == 0) {
            // 分配失败，释放已分配的页
            for (uint64 b = oldpa; b < a; b += PGSIZE) {
              kfree((void *)b);
            }
            release(&shm_lock);
            return -1;
          }
          memset(mem, 0, PGSIZE); // 清零分配的内存
        }

        shm_table[i].start_pa = oldpa;
        shm_table[i].end_pa = newpa;
        release(&shm_lock);
        return shm_table[i].shmid;
      }
    }
  }

  // 没有找到共享内存段且未设置 IPC_CREAT
  release(&shm_lock);
  return -1;
}

uint64
shmat(int shmid, uint64 addr) {
  acquire(&shm_lock);
  for (int i = 0; i < MAX_SHARED_SEGMENTS; i++) {
    if (shm_table[i].shmid == shmid) {
      // 找到共享内存段
      uint64 start_pa = shm_table[i].start_pa;
      uint64 end_pa = shm_table[i].end_pa;
      uint64 size = end_pa - start_pa;
      release(&shm_lock);

      // 如果用户没有提供地址，自动分配虚拟地址
      if (addr == 0) {
        addr = TRAPFRAME - PGROUNDUP(size); // 将地址分配到 trapframe 下方
        printf("trapframe: %p\n", (char *)TRAPFRAME);
        printf("Auto-allocated address: %p\n", (char *)addr);
      }

      printf("Physical address: %p\n", (char *)start_pa);
      // 映射共享内存段到进程的地址空间
      if (mappages(myproc()->pagetable, addr, size, start_pa, PTE_R | PTE_W | PTE_U) < 0) {
        return -1;
      }
      shm_table[i].va = addr; // 保存映射的虚拟地址
      shm_table[i].ref_count++;
      return addr;
    }
  }
  // 没有找到共享内存段
  release(&shm_lock);
  return -1;
}

int
shmdt(uint64 shmaddr) {
  struct proc *p = myproc();
  pagetable_t pagetable = p->pagetable;

  acquire(&shm_lock);

  // 遍历共享内存表，找到与地址匹配的共享内存段
  for (int i = 0; i < MAX_SHARED_SEGMENTS; i++) {
    if (shm_table[i].va == shmaddr) {
      // 找到匹配的共享内存段
      uint64 start_pa = shm_table[i].start_pa;
      uint64 end_pa = shm_table[i].end_pa;
      uint64 size = end_pa - start_pa;

      // 解除映射
      uvmunmap(pagetable, shmaddr, size / PGSIZE, 1);
      shm_table[i].ref_count--;
      printf("shmdt: Unmapping shared memory segment %d\n", shm_table[i].shmid);

      if (shm_table[i].ref_count == 0) {
        // 如果引用计数为0，释放共享内存段
        shm_table[i].key = -1;
        shm_table[i].start_pa = 0;
        shm_table[i].end_pa = 0;
        shm_table[i].size = 0;
        shm_table[i].ref_count = 0;
        shm_table[i].shmid = -1;
      }

      release(&shm_lock);
      return 0; // 成功解除映射
    }
  }

  release(&shm_lock);
  return -1; // 未找到匹配的共享内存段
}