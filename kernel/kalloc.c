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
  char    lock_name[16];
  int     free_cnt;
} kmem[NCPU];

void
kinit()
{
    for(int i=0;i<NCPU;i++){
        int off=snprintf(kmem[i].lock_name,15,"kmem_%d",i);
        kmem[i].lock_name[off]='\0';
        printf("%s\n",kmem[i].lock_name);
        initlock(&kmem[i].lock,kmem[i].lock_name);
    }
  //initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{

  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
    push_off();

  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
  pop_off();
}

// Free the page of physical memory pointed at by v,
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

  push_off();
  int i=cpuid();
  acquire(&kmem[i].lock);
  r->next = kmem[i].freelist;
  kmem[i].freelist = r;
  kmem[i].free_cnt++;
  release(&kmem[i].lock);
  pop_off();
}
//must irq off and has free list lock of cpuid
struct run* steal_half_free_block(int cpuid,int* get_cnt){
    struct run* res=0;
    int k=0,left_block=0;
    for(int j=0;j<NCPU;j++){
        if(j!=cpuid){
            acquire(&kmem[j].lock);
            k=kmem[j].free_cnt;
            if(k>0){
                res=kmem[j].freelist;
                if(k<=32){//全部拿走
                    left_block=0;
                    kmem[j].freelist=0;
                }else{//剩下32个
                    left_block=32;
                    struct run* cur=res;
                    for(int l=0;l<31;l++){
                        cur=cur->next;
                    }
                    res=cur->next;
                    cur->next=0;
                }
                kmem[j].free_cnt=left_block;
                *get_cnt=k-left_block;
                release(&kmem[j].lock);
                return res;
            }
            release(&kmem[j].lock);
        }
    }
    *get_cnt=0;
    return 0;
}
// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  push_off();
  int i=cpuid();
  acquire(&kmem[i].lock);
  r = kmem[i].freelist;
  if(r){
      kmem[i].freelist = r->next;
      kmem[i].free_cnt--;
      goto back;
  }else{
      int get_cnt=0;
      struct run* get=steal_half_free_block(i,&get_cnt);
      if(get==0){
          goto back;
      }else{
          kmem[i].freelist=get->next;
          kmem[i].free_cnt=get_cnt-1;
          r=get;
      }
  }

back:
  release(&kmem[i].lock);
  if(r){
      memset((char*)r, 5, PGSIZE); // fill with junk
  }

    pop_off();
  return (void*)r;
}
