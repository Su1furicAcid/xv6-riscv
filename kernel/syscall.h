// System call numbers
#define SYS_fork    1
#define SYS_exit    2
#define SYS_wait    3
#define SYS_pipe    4
#define SYS_read    5
#define SYS_kill    6
#define SYS_exec    7
#define SYS_fstat   8
#define SYS_chdir   9
#define SYS_dup    10
#define SYS_getpid 11
#define SYS_sbrk   12
#define SYS_sleep  13
#define SYS_uptime 14
#define SYS_open   15
#define SYS_write  16
#define SYS_mknod  17
#define SYS_unlink 18
#define SYS_link   19
#define SYS_mkdir  20
#define SYS_close  21
// 系统调用：返回进程数量
#define SYS_getprocnum 22
// 系统调用：演示分配器
#define SYS_demo_allocator 23
// 系统调用：我的sbrk
#define SYS_mysbrk 24
// 系统调用：设置进程优先级
#define SYS_setpriority 25
// 系统调用：获取进程优先级
#define SYS_getpriority 26
#define SYS_shmget 27
#define SYS_shmat 28
#define SYS_shmdt 29
#define SYS_shmrel 30