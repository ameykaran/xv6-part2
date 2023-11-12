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

#define PA_COUNT (PGROUNDUP(PHYSTOP) / PGSIZE)
struct {
  struct spinlock lock;
  struct run *freelist;
  int pte_count[PA_COUNT];
} kmem;

void
inc_count(void *pa)
{
  kmem.pte_count[(uint64)pa / PGSIZE] += 1;
}

void
dec_count(void *pa)
{
  kmem.pte_count[(uint64)pa / PGSIZE] -= 1;
}

void
safe_inc_count(void *pa)
{
  acquire(&kmem.lock);
  inc_count(pa);
  release(&kmem.lock);
}

void
set_count(void *pa, int cnt)
{
  kmem.pte_count[(uint64)pa / PGSIZE] = cnt;
}

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  memset(&kmem.pte_count, 0, sizeof(kmem.pte_count));
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
  {
    // set the counter to 1 initially
    set_count(p, 1);
    kfree(p);
  }
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

  acquire(&kmem.lock);
  dec_count(pa);
  // if more than one process point to the page, decerease the counter;
  // else free the page table entry
  if (kmem.pte_count[(uint64)pa/PGSIZE] > 0)
  {
    release(&kmem.lock);
    return;
  }

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

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
  {
    memset((char*)r, 5, PGSIZE); // fill with junk
    set_count(r, 1);
  }
  
  return (void*)r;
}
