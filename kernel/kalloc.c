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
struct metadata_entry {
  void *addr;  
  int order;   
};

struct metadata_manager {
  struct metadata_entry *entries; 
  int capacity;                   
  int count;                      
  struct spinlock lock;        
};

struct metadata_manager meta_manager;

void* buddysystem_alloc(int order, int skip_metadata);
void buddysystem_free(void *pa, int skip_metadata);

void metadata_manager_init() {
  initlock(&meta_manager.lock, "meta_manager");

  meta_manager.entries = (struct metadata_entry*)buddysystem_alloc(6, 1);
  if (!meta_manager.entries)
    panic("Failed to allocate metadata manager");

  meta_manager.capacity = PGSIZE / sizeof(struct metadata_entry); 
  meta_manager.count = 0;
}

void add_metadata(void *addr, int order) {
  acquire(&meta_manager.lock);

  if (meta_manager.count >= meta_manager.capacity) {
    release(&meta_manager.lock);

    struct metadata_entry *new_entries = (struct metadata_entry*)buddysystem_alloc(6, 1);
    if (!new_entries)
      panic("Failed to expand metadata manager");

    memmove(new_entries, meta_manager.entries, meta_manager.count * sizeof(struct metadata_entry));
    buddysystem_free(meta_manager.entries, 0);
    meta_manager.entries = new_entries;
    meta_manager.capacity += PGSIZE / sizeof(struct metadata_entry);
  }

  meta_manager.entries[meta_manager.count].addr = addr;
  meta_manager.entries[meta_manager.count].order = order;
  meta_manager.count++;

  release(&meta_manager.lock);
}

int find_metadata(void *addr) {
  acquire(&meta_manager.lock);

  for (int i = 0; i < meta_manager.count; i++) {
    if (meta_manager.entries[i].addr == addr) {
      int order = meta_manager.entries[i].order;
      release(&meta_manager.lock);
      return order;
    }
  }

  release(&meta_manager.lock);
  panic("Metadata not found for address");
  return -1;
}

void delete_metadata(void *addr) {
  acquire(&meta_manager.lock);

  for (int i = 0; i < meta_manager.count; i++) {
    if (meta_manager.entries[i].addr == addr) {
      meta_manager.entries[i] = meta_manager.entries[meta_manager.count - 1];
      meta_manager.count--;
      release(&meta_manager.lock);
      return;
    }
  }

  release(&meta_manager.lock);
  panic("Metadata not found for address");
}

void buddysystem_free(void *pa, int skip_metadata) {
  int order;

  if (!skip_metadata) {
    order = find_metadata(pa);
    delete_metadata(pa);
  } else {
    order = 6;
  }

  acquire(&buddy_system.lock);

  while (order < MAX_ORDER - 1) {
    uint64 buddy_pa = ((uint64)pa ^ (1 << (order + UNIT_SIZE_LOG2)));
    struct run *buddy = (struct run*)buddy_pa;

    struct run **freelist = &buddy_system.freelist[order];
    struct run *prev = 0;
    struct run *curr = *freelist;
    while (curr) {
      if (curr == buddy) {
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
      break;

    if ((uint64)pa > buddy_pa)
      pa = (void*)buddy_pa;
    order++;
  }

  struct run *r = (struct run*)pa;
  r->next = buddy_system.freelist[order];
  buddy_system.freelist[order] = r;

  release(&buddy_system.lock);
}

void* buddysystem_alloc(int order, int skip_metadata) {
  acquire(&buddy_system.lock);
  struct run *r = 0;

  for (int i = order; i < MAX_ORDER; i++) {
    if (buddy_system.freelist[i]) {
      r = buddy_system.freelist[i];
      buddy_system.freelist[i] = r->next;

      while (i > order) {
        i--;
        uint64 buddy_pa = (uint64)r + (1 << (i + UNIT_SIZE_LOG2));
        struct run *buddy = (struct run*)buddy_pa;

        buddy->next = buddy_system.freelist[i];
        buddy_system.freelist[i] = buddy;
      }

      release(&buddy_system.lock);

      if (!skip_metadata) add_metadata((void*)r, order);

      return (void*)r;
    }
  }

  printf("Buddy allocation failed: no free blocks for order %d\n", order);
  release(&buddy_system.lock);
  return 0;
}

// initialize the buddy system
void buddysystem_init(void* start, void* end) {
  initlock(&buddy_system.lock, "buddy_system");
  for (int i = 0; i < MAX_ORDER; i++) {
    buddy_system.freelist[i] = 0;
  }

  char* p = (char*)PGROUNDUP((uint64)start);
  for (; p + PGSIZE <= (char*)end; p += PGSIZE) {
    buddysystem_free((void*)p, 1);
  }

  metadata_manager_init();
}

// implement kmalloc and kmfree using buddy system
void* malloc(int size) {
  if (size == 0) {
    return 0;
  }

  int order = 0;

  while ((1 << order) * UNIT_SIZE < size)
    order++;

  void* addr = buddysystem_alloc(order, 0);
  if (addr == 0) {
    printf("malloc: failed to allocate memory of size %d\n", size);
    return 0;
  }

  return addr;
}

void mfree(void *pa) {
  if (pa == 0) {
    printf("mfree: attempt to free null pointer\n");
    return;
  }

  buddysystem_free(pa, 0);
}