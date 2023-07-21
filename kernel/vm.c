
#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "spinlock.h"
#include "proc.h"

/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;

extern char etext[];  // kernel.ld sets this to end of kernel code.

extern char trampoline[]; // trampoline.S

// Make a direct-map page table for the kernel.
pagetable_t
kvmmake(void)
{
  pagetable_t kpgtbl;

  kpgtbl = (pagetable_t) kalloc();
  memset(kpgtbl, 0, PGSIZE);

  // uart registers
  kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // PLIC
  kvmmap(kpgtbl, PLIC, PLIC, 0x400000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  kvmmap(kpgtbl, KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  kvmmap(kpgtbl, (uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

  // allocate and map a kernel stack for each process.
  proc_mapstacks(kpgtbl);
  
  return kpgtbl;
}

// Initialize the one kernel_pagetable
void
kvminit(void)
{
  kernel_pagetable = kvmmake();
}

// Switch h/w page table register to the kernel's page table,
// and enable paging.
void
kvminithart()
{
  // wait for any previous writes to the page table memory to finish.
  sfence_vma();

  w_satp(MAKE_SATP(kernel_pagetable));

  // flush stale entries from the TLB.
  sfence_vma();
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if(va >= MAXVA)
    panic("walk");

  for(int level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)];
    if(*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte);
    } else {
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0)
        return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  return &pagetable[PX(0, va)];
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if(va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if(pte == 0)
    return 0;
  if((*pte & PTE_V) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);
  return pa;
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void
kvmmap(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, int perm)
{
  if(mappages(kpgtbl, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned. Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;

  if(size == 0)
    panic("mappages: size");
  
  a = PGROUNDDOWN(va);
  last = PGROUNDDOWN(va + size - 1);
  for(;;){
    if((pte = walk(pagetable, a, 1)) == 0)
      return -1;
    if(*pte & PTE_V) {
      panic("mappages: remap");
    }
    *pte = PA2PTE(pa) | perm | PTE_V;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

#if SWAP_ALGO != NONE

 void remove_scfifo(struct proc *p, int position) {

    struct scfifo *scfifo = &p->scfifo[position];
    if (scfifo == p->newest && scfifo == p->oldest) {
      p->oldest = 0;
      p->newest = 0;
    } else if (scfifo == p->oldest) {
      p->oldest = p->oldest->newer;
      p->oldest->older = 0;
    } else if (scfifo == p->newest) {
      p->newest = p->newest->older;
      p->newest->newer = 0;
    } else {
      scfifo->older->newer = scfifo->newer;
      scfifo->newer->older = scfifo->older;
    }
    scfifo->newer = 0;
    scfifo->older = 0;
  }

  int freePage(struct page *pages, uint64 va) {
    struct page *page;
    int i = 0;
    for (page = pages; page < &pages[MAX_PSYC_PAGES]; page++) {
      if (page->va == va) {
        page->counter = 0;
        page->va = 0;
        page->status = UNUSED;
        return i;
      }
      i++;
    }
    panic("couldn't find page");
    return -1;
  }
#endif

// Remove npages of mappings starting from va. va must be
// page-aligned. The mappings must exist.
// Optionally free the physical memory.
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;
  #if SWAP_ALGO != NONE
    struct proc *p = myproc();
    int sh_init = sh_or_init(p);
  #endif

  if((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    if((pte = walk(pagetable, a, 0)) == 0)
      panic("uvmunmap: walk");
    if((*pte & (PTE_V | PTE_PG)) == 0)
      panic("uvmunmap: not mapped");
    if(PTE_FLAGS(*pte) == PTE_V)
      panic("uvmunmap: not a leaf");
    if(do_free && (*pte & PTE_V)){
      #if SWAP_ALGO != NONE
        if (sh_init && pagetable == p->pagetable) {
          #if SWAP_ALGO == SCFIFO
            int position = freePage(p->memory_pages, a);
            remove_scfifo(p, position);
          #else
            freePage(p->memory_pages, a);
          #endif
          p->num_of_phys_pages--;
        }
      #endif
      uint64 pa = PTE2PA(*pte);
      kfree((void*)pa);
    }
    #if SWAP_ALGO != NONE
      if (sh_init && pagetable == p->pagetable && (*pte & PTE_PG)) {
        freePage(p->swapfile_pages, a);
      }
    #endif
    *pte = 0;
  }
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t
uvmcreate()
{
  pagetable_t pagetable;
  pagetable = (pagetable_t) kalloc();
  if(pagetable == 0)
    return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Load the user initcode into address 0 of pagetable,
// for the very first process.
// sz must be less than a page.
void
uvmfirst(pagetable_t pagetable, uchar *src, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("uvmfirst: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W|PTE_R|PTE_X|PTE_U);
  memmove(mem, src, sz);
}

// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int xperm)
{
  char *mem;
  uint64 a;
  #if SWAP_ALGO != NONE
    int sh_init = sh_or_init(myproc()); 
    if (sh_init && newsz >= MAX_TOTAL_PAGES * PGSIZE)
      return 0;
  #endif
  if(newsz < oldsz)
    return oldsz;
  oldsz = PGROUNDUP(oldsz);
  for(a = oldsz; a < newsz; a += PGSIZE) {
    mem = kalloc();
    if(mem == 0){
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_R|PTE_U|xperm) != 0){
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    #if SWAP_ALGO != NONE
      if (sh_init) {
        allocate_page(pagetable, a); 
      }
    #endif
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if(newsz >= oldsz)
    return oldsz;

  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
  }

  return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void
freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if(pte & PTE_V){
      panic("freewalk: leaf");
    }
  }
  kfree((void*)pagetable);
}

// Free user memory pages,
// then free page-table pages.
void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  if(sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz)/PGSIZE, 1);
  freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if((*pte & (PTE_V | PTE_PG)) == 0)
      panic("uvmcopy: page not present");
    if (*pte & PTE_V) {
      pa = PTE2PA(*pte);
      flags = PTE_FLAGS(*pte);
      if((mem = kalloc()) == 0)
        goto err;
      memmove(mem, (char*)pa, PGSIZE);
      if(mappages(new, i, PGSIZE, (uint64)mem, flags) != 0){
        kfree(mem);
        goto err;
      }
    } else {
      pte_t *npte;
      npte = walk(new, i, 0);
      *npte |= PTE_FLAGS(*pte);
    }
  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void
uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  
  pte = walk(pagetable, va, 0);
  if(pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > len)
      n = len;
    memmove(dst, (void *)(pa0 + (srcva - va0)), n);

    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int
copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  uint64 n, va0, pa0;
  int got_null = 0;

  while(got_null == 0 && max > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > max)
      n = max;

    char *p = (char *) (pa0 + (srcva - va0));
    while(n > 0){
      if(*p == '\0'){
        *dst = '\0';
        got_null = 1;
        break;
      } else {
        *dst = *p;
      }
      --n;
      --max;
      p++;
      dst++;
    }

    srcva = va0 + PGSIZE;
  }
  if(got_null){
    return 0;
  } else {
    return -1;
  }
}
#if SWAP_ALGO != NONE
  void set_scfifo(struct proc *p, int position) {
    struct scfifo *scfifo = &p->scfifo[position];
    scfifo->position = position;
    scfifo->newer = 0;
    scfifo->older = p->newest;
    if (p->newest != 0)
      p->newest->newer = scfifo;
    p->newest = scfifo;
    if (p->oldest == 0) {
      p->oldest = scfifo;
      scfifo->older = 0;
    } 
  }

  #if SWAP_ALGO == LAPA
  uint ones(uint64 counter) {
    uint counter_ones = 0;
    while (counter != 0) {
      counter_ones += counter & 1;
      counter >>= 1;
    }
    return counter_ones;
  }
  #endif

  struct page* findPageToEvict(struct page *pages) {
    struct page *min_page = 0;
    #if SWAP_ALGO == SCFIFO
      pte_t *pte;
      struct proc *p = myproc();
      while (min_page == 0) {
        int position = p->oldest->position;
        min_page = &pages[position];
        pte = walk(p->pagetable, min_page->va, 0);
        if ((*pte & PTE_A) == 0) 
          return min_page;
        *pte &= ~PTE_A;
        remove_scfifo(p, position);
        set_scfifo(p, position);
        min_page = 0;
      }
      return 0;
    #else
      struct page *page;
      uint min_ones = 100;
      for (page = pages; page < &pages[MAX_PSYC_PAGES]; page++) {
        if (page->status == INMEMORY) {
          #if SWAP_ALGO == NFUA
            if (min_page == 0 || page->counter < min_page->counter) {
              min_page = page;
            }
          #endif
          #if SWAP_ALGO == LAPA
            uint page_ones;
            page_ones = ones(page->counter);
            if (page_ones < min_ones || (page_ones == min_ones && page->counter < min_page->counter)) {
              min_page = page;
              min_ones = page_ones;
            }
          #endif
        }
      }
      if (min_page == 0)
        panic("no min page");
      return min_page;
    #endif
  }

  int findFree(struct page *pages) {
    for (int i = 0; i < MAX_PSYC_PAGES; i++) {
      if (pages[i].status == UNUSED)
        return i;
    }
    panic("cant find free");
    return -1;
  }
  
  int findPageLocation(struct page *pages, uint64 va) {
    for (int i = 0; i < MAX_PSYC_PAGES; i++) {
      if (pages[i].va == va) {
        pages[i].status = UNUSED;
        return i;
      }
    }
    panic("cant find page");
    return -1;
  }

  void swap_out(pagetable_t pagetable) {
    struct proc *p = myproc();
    pte_t *pte;
    uint64 pa;
    struct page *swapfile_page;
    struct page *memory_page;
    int position;
    position = findFree(p->swapfile_pages);
    swapfile_page = &p->swapfile_pages[position];
    memory_page = findPageToEvict(p->memory_pages);
    swapfile_page->va = memory_page->va;
    swapfile_page->status = PAGED;
    memory_page->va = 0;
    memory_page->status = UNUSED;
    p->num_of_phys_pages--;
    pte = walk(pagetable, swapfile_page->va, 0);
    pa = PTE2PA(*pte);
    writeToSwapFile(p, (char*)pa, position * PGSIZE, PGSIZE);
    *pte &= ~PTE_V;
    *pte |= PTE_PG;
    kfree((void *)pa);
  }

  void set_counter(struct page *page) {
    #if SWAP_ALGO == NFUA
      page->counter = 0;
    #elif SWAP_ALGO == LAPA
      page->counter = 0xFFFFFFFFFFFFFFFF;
    #endif
  }

  void allocate_page(pagetable_t pagetable, uint64 va) {
    struct proc *p = myproc();
    struct page *page;
    int position;
    
    if (p->num_of_phys_pages == MAX_PSYC_PAGES)
      swap_out(pagetable);
    position = findFree(p->memory_pages);
    page = &p->memory_pages[position];
    page->status = INMEMORY;
    page->va = va;
    #if SWAP_ALGO == SCFIFO
      set_scfifo(p, position);
    #else
      set_counter(page);
    #endif
    p->num_of_phys_pages++;
  }

  int page_fault(struct proc *p, uint64 va) {
    pte_t *pte;
    char *mem;
    int position;
    pte = walk(p->pagetable, va, 0);
    if ((*pte & PTE_PG) == 0) {
      return 0;             // Seg fault
    }
    mem = kalloc();
    position = findPageLocation(p->swapfile_pages, va);
    readFromSwapFile(p, mem, position * PGSIZE, PGSIZE);
    allocate_page(p->pagetable, va);
    *pte = PA2PTE((uint64)mem) | PTE_FLAGS(*pte);
    *pte &= ~PTE_PG;
    *pte |= PTE_V;
    return 3;
  }

  void update_counters(struct proc *p) {
    struct page *page;
    pte_t *pte;
    for (page = p->memory_pages; page < &p->memory_pages[MAX_PSYC_PAGES]; page++) {
      if (page->status == INMEMORY) {
        pte = walk(p->pagetable, page->va, 0);
        page->counter = page->counter >> 1;
        if (*pte & PTE_A) {
          page->counter |= 0x8000000000000000;
          *pte &= ~PTE_A;
        }
      }
    }
  }

int sh_or_init(struct proc *p) {
  int size = sizeof(p->name);
  if (strncmp(p->name, "sh", size) == 0)
    return 0;
  if (strncmp(p->name, "init", size) == 0)
    return 0;
  if (strncmp(p->name, "initcode", size) == 0)
    return 0;
  return 1;
}

#endif