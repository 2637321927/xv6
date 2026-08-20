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
struct {
  struct spinlock lock;
  int arr[NPAGE];
} refcr;
void 
refinc(uint64 pa)
{
  acquire(&refcr.lock);
  refcr.arr[INDEX(pa)]++;
  release(&refcr.lock);
}
void 
refdes(uint64 pa)
{
  acquire(&refcr.lock);
  refcr.arr[INDEX(pa)]--;
  release(&refcr.lock);
}
void 
refset(uint64 pa, int n)
{
  acquire(&refcr.lock);
  refcr.arr[INDEX(pa)] = n;
  release(&refcr.lock);
}
uint64 
refget(uint64 pa)
{
  uint64 val;
  acquire(&refcr.lock);
  val = refcr.arr[INDEX(pa)];
  release(&refcr.lock);
  return val;
}
void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}
void 
kinit(void)
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
  initlock(&refcr.lock, "refcr");
  memset(refcr.arr, 0, sizeof(refcr.arr));
}

void 
*kalloc(void)
{
  struct run *r;
  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r){
    kmem.freelist = r->next;
    refset((uint64)r, 1);
  }
  release(&kmem.lock);
  if(r) memset((char*)r,5,PGSIZE);
  return r;
}

void 
kfree(void *pa)
{
  if(((uint64)pa%PGSIZE)!=0 || (char*)pa<end || (uint64)pa>=PHYSTOP)
    panic("kfree");

  if(refget((uint64)pa) > 1){
    refdes((uint64)pa);
    return;
  }
  refset((uint64)pa, 0);
  memset(pa,1,PGSIZE);
  struct run *r = (struct run*)pa;
  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

