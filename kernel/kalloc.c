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

// free a page
void buddysystem_free(void *pa, int order) {
  acquire(&buddy_system.lock);

  // Try to merge with buddy blocks
  while (order < MAX_ORDER - 1) {
    uint64 buddy_pa = ((uint64)pa ^ (1 << (order + UNIT_SIZE_LOG2))); // Calculate buddy address
    struct run *buddy = (struct run*)buddy_pa;

    // Check if the buddy block is free and of the same order
    struct run **freelist = &buddy_system.freelist[order];
    struct run *prev = 0;
    struct run *curr = *freelist;
    while (curr) {
      if (curr == buddy) {
        // Remove buddy from free list
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
      break; // Buddy block is not free, stop merging

    // Merge with buddy block
    if ((uint64)pa > buddy_pa)
      pa = (void*)buddy_pa;
    order++;
  }

  // Add the merged block to the free list
  struct run *r = (struct run*)pa;
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

      release(&buddy_system.lock);
      return (void*)r;
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
    buddysystem_free(p, 6);
  }
}

// implement kmalloc and kmfree using buddy system
void* malloc(int size) {
  int order = 0;
  while ((1 << order) * UNIT_SIZE < size)
    order++;
  return buddysystem_alloc(order);
}

void mfree(void *pa, int size) {
  int order = 0;
  while ((1 << order) * UNIT_SIZE < size)
    order++;
  buddysystem_free(pa, order);
}