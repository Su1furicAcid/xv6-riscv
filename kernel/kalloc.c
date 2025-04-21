// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

void buddysystem_init(void* start, void* end);

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)BUDDY_START);
  // 初始化伙伴系统
  buddysystem_init((void*)BUDDY_START, (void*)BUDDY_END);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}


// -----------------------------------------------
// Allocate physical memory using buddy system

// range from 64B to 16MB
// order 0: 64B, ..., 6: 4KB, ..., 18: 16MB
#define MAX_ORDER 19
#define UNIT_SIZE (1 << 6) // 64B
#define UNIT_SIZE_LOG2 6

// define the buddy system
struct buddy {
  // 11 free lists corresponding to 11 levels
  struct run *freelist[MAX_ORDER];
  // lock for buddy system
  struct spinlock lock;
};

struct buddy buddy_system;

// metadata for block after allocation
struct header {
  int order;
};

// free a page
void buddysystem_free(void *pa) {
  // 获取 header 的地址
  struct header *hdr = (struct header*)((char*)pa - sizeof(struct header));
  int order = hdr->order; // 从 header 中获取 order

  acquire(&buddy_system.lock);

  // 尝试合并伙伴块
  while (order < MAX_ORDER - 1) {
    uint64 buddy_pa = ((uint64)hdr ^ (1 << (order + UNIT_SIZE_LOG2))); // 计算伙伴地址
    struct run *buddy = (struct run*)buddy_pa;

    // 检查伙伴块是否空闲并且阶数相同
    struct run **freelist = &buddy_system.freelist[order];
    struct run *prev = 0;
    struct run *curr = *freelist;
    while (curr) {
      if (curr == buddy) {
        // 从空闲链表中移除伙伴块
        if (prev)
          prev->next = curr->next;
        else
          *freelist = curr->next;
        break;
      }
      prev = curr;
      curr = curr->next;
    }

    if (!curr)
      break; // 伙伴块不可用，停止合并

    // 合并当前块和伙伴块
    if ((uint64)hdr > buddy_pa)
      hdr = (struct header*)buddy_pa;
    order++;
  }

  // 将合并后的块加入空闲链表
  struct run *r = (struct run*)hdr;
  r->next = buddy_system.freelist[order];
  buddy_system.freelist[order] = r;

  release(&buddy_system.lock);
}

void* buddysystem_alloc(int order) {
  acquire(&buddy_system.lock);
  struct run *r = 0;

  // 从指定的 order 开始向上查找更大的块
  for (int i = order; i < MAX_ORDER; i++) {
    if (buddy_system.freelist[i]) {
      r = buddy_system.freelist[i];
      buddy_system.freelist[i] = r->next;

      // 如果找到的块比需要的块大，则切分
      while (i > order) {
        i--;
        uint64 buddy_pa = (uint64)r + (1 << (i + UNIT_SIZE_LOG2)); // 计算伙伴地址
        struct run *buddy = (struct run*)buddy_pa;

        // 将伙伴块加入到更小的 order 的空闲链表中
        buddy->next = buddy_system.freelist[i];
        buddy_system.freelist[i] = buddy;
      }

      // 在分配的块头部存储 header 信息
      struct header *hdr = (struct header*)r;
      hdr->order = order;

      release(&buddy_system.lock);
      return (void*)((char*)r + sizeof(struct header)); // 返回用户数据区域
    }
  }

  // 如果没有找到合适的块，分配失败
  printf("Buddy allocation failed: no free blocks for order %d\n", order);
  release(&buddy_system.lock);
  return 0;
}

// initialize the buddy system
void buddysystem_init(void* start, void* end) {
  initlock(&buddy_system.lock, "buddy_system");
  for (int i = 0; i < MAX_ORDER; i++) {
    // initialize the free list using NULL
    buddy_system.freelist[i] = 0;
  }

  char* p = (char*)PGROUNDUP((uint64)start);
  for (; p + PGSIZE <= (char*)end; p += PGSIZE) {
    // 设置 header 的 order 值
    struct header *hdr = (struct header*)p;
    hdr->order = 6; // 每个页块的大小为 4KB，对应 order = 6

    // 将块释放到伙伴系统
    buddysystem_free((void*)(p + sizeof(struct header)));
  }
}

// implement kmalloc and kmfree using buddy system
void* malloc(int size) {
  int order = 0;
  while ((1 << order) * UNIT_SIZE < size + sizeof(struct header)) // 包括 header 的大小
    order++;
  return buddysystem_alloc(order);
}

void mfree(void *pa) {
  buddysystem_free(pa);
}