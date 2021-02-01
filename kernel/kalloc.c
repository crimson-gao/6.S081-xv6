// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"
#include "proc.h"
#define ALL_PAGES (1<<15)
void freerange(void *pa_start, void *pa_end);
struct{
    char cnt[(PHYSTOP-KERNBASE)/PGSIZE];
} page_ref_cnt;
extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;
#define PAGE_INDEX(pa) ((PGROUNDDOWN(pa)-KERNBASE)/PGSIZE)


void increase_cnt(uint64 pa){
    page_ref_cnt.cnt[PAGE_INDEX(pa)]++;
}

void decrease_cnt(uint64 pa){
    char cur=page_ref_cnt.cnt[PAGE_INDEX(pa)];
    if(cur>0)page_ref_cnt.cnt[PAGE_INDEX(pa)]=cur-1;
}
char get_page_cnt(uint64 pa){
    return page_ref_cnt.cnt[PAGE_INDEX(pa)];
}
void set_page_cnt(uint64 pa,char a){
    page_ref_cnt.cnt[PAGE_INDEX(pa)]=a;
}

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  memset(&page_ref_cnt,0,sizeof(page_ref_cnt));
  freerange((void*)end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
    struct run *r;
    char cnt=get_page_cnt((uint64)pa);
    if(cnt>1){
        decrease_cnt((uint64)pa);
        if(get_page_cnt((uint64)pa)!=cnt-1)panic("kfree panic");
        return;
    }

    if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
        panic("kfree");

    // Fill with junk to catch dangling refs.
    memset(pa, 1, PGSIZE);
    set_page_cnt((uint64)pa,0);
    if(get_page_cnt((uint64)pa)!=0)panic("kfree panic 2");
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
  if(r){
      set_page_cnt((uint64)r,1);
      if(get_page_cnt((uint64)r)!=1)panic("kalloc");
  }

  return (void*)r;
}
