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
void slab_init(void);

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)BUDDY_START);
  // 初始化伙伴系统和slab分配器
  buddysystem_init((void*)BUDDY_START, (void*)BUDDY_END);
  slab_init();
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
// Allocate physical memory using buddy system and slab allocator

// range from 4KB(1 page) to 16MB(4096 pages), 13 levels
// order 0: 4KB, 1: 8KB, 2: 16KB, ..., 11: 8MB, 12: 16MB
#define MAX_ORDER 13

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
    uint64 buddy_pa = ((uint64)pa ^ (1 << (order + 12))); // Calculate buddy address
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
        uint64 buddy_pa = (uint64)r + (1 << (i + 12)); // 计算伙伴地址
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
    buddysystem_free(p, 0);
  }

  for (int i = 0; i < MAX_ORDER; i++) {
    printf("Order %d: ", i);
    struct run *curr = buddy_system.freelist[i];
    while (curr) {
      printf("%p -> ", curr);
      curr = curr->next;
    }
    printf("NULL\n");
  }
}

// define the slab allocator
struct slab {
  struct run *freelist;
  struct spinlock lock;
  int object_size;
};

struct slab slab_allocator;

// allocate an object from the slab allocator
void* slab_alloc(void) {
  acquire(&slab_allocator.lock);

  // 如果 freelist 为空，从伙伴系统分配一页并初始化
  if (!slab_allocator.freelist) {
    void *page = buddysystem_alloc(0); // 分配一页
    if (!page) {
      release(&slab_allocator.lock);
      printf("Slab allocation failed: no free blocks\n");
      return 0;
    }

    // 将这一页分割成多个小对象
    for (int i = 0; i < PGSIZE / slab_allocator.object_size; i++) {
      struct run *r = (struct run*)((char*)page + i * slab_allocator.object_size);
      r->next = slab_allocator.freelist;
      slab_allocator.freelist = r;
    }
  }

  // 从 freelist 中分配一个对象
  struct run *r = slab_allocator.freelist;
  slab_allocator.freelist = r->next;
  release(&slab_allocator.lock);
  return (void*)r;
}

// free an object to the slab allocator
void slab_free(void *pa) {
  acquire(&slab_allocator.lock);
  struct run *r = (struct run*)pa;
  r->next = slab_allocator.freelist;
  slab_allocator.freelist = r;
  release(&slab_allocator.lock);
}

// initialize the slab allocator
void slab_init(void) {
  initlock(&slab_allocator.lock, "slab_allocator");
  slab_allocator.object_size = 64;
  slab_allocator.freelist = 0;

  // 初始化 Slab 分配器的空闲链表
  for (int i = 0; i < 4; i++) { // 假设初始化 4 页内存
    void *page = buddysystem_alloc(0); // 从伙伴系统分配一页
    if (!page) {
      printf("Slab initialization failed: no free blocks\n");
      break;
    }

    // 将这一页分割成多个小对象
    for (int j = 0; j < PGSIZE / slab_allocator.object_size; j++) {
      struct run *r = (struct run*)((char*)page + j * slab_allocator.object_size);
      r->next = slab_allocator.freelist;
      slab_allocator.freelist = r;
    }
  }
}

// implement kmalloc and kmfree using buddy system and slab allocator
void* malloc(int size) {
  if (size <= slab_allocator.object_size) {
    return slab_alloc();
  } else {
    int order = 0;
    while ((1 << order) * PGSIZE < size)
      order++;
    return buddysystem_alloc(order);
  }
}

void mfree(void *pa, int size) {
  if (size <= slab_allocator.object_size) {
    slab_free(pa);
  } else {
    int order = 0;
    while ((1 << order) * PGSIZE < size)
      order++;
    buddysystem_free(pa, order);
  }
}