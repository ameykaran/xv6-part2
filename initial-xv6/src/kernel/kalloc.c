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

#define PA_NUM (PGROUNDUP(PHYSTOP) / PGSIZE)

struct spinlock pte_lock;
int pte_count[PA_NUM];

void
inc_pte_count(void *pa)
{
  acquire(&pte_lock);
  pte_count[(uint64)pa / PGSIZE] += 1;
  release(&pte_lock);
}

void
dec_pte_count(void *pa)
{
  acquire(&pte_lock);
  pte_count[(uint64)pa / PGSIZE] -= 1;
  release(&pte_lock);
}

void
set_pte_count(void *pa, int cnt)
{
  acquire(&pte_lock);
  pte_count[(uint64)pa / PGSIZE] = cnt;
  release(&pte_lock);
}

void
kinit()
{
  initlock(&pte_lock, "ptelock");

  initlock(&kmem.lock, "kmem");
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
    acquire(&pte_lock);
    pte_count[(uint64)p/PGSIZE] = 1;
    release(&pte_lock);

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

  dec_pte_count(pa);
  acquire(&pte_lock);
  // if more than one process point to the page, decerease the counter;
  // else free the page table entry
  if (pte_count[(uint64)pa/PGSIZE] > 0)
  {
    release(&pte_lock);
    return;
  }
  release(&pte_lock);

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
  {
    memset((char*)r, 5, PGSIZE); // fill with junk
    set_pte_count(r, 1);
  }
  
  return (void*)r;
}
