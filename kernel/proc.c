#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

struct storedpage* get_free_storedpage(void);
struct storedpage* get_wanted_storedpage(uint64);
void update_access_counters(struct proc *p);
int count_ones(uint);
uint64 find_nfu(void);
uint64 find_lapa(void);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl) {
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int) (p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table at boot time.
void
procinit(void)
{
  struct proc *p;
  
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      p->kstack = KSTACK((int) (p - proc));
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void) {
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void) {
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int
allocpid() {
  int pid;
  
  acquire(&pid_lock);
  pid = nextpid;
  // printf("process %d  acquired %s lock\n", pid, pid_lock.name);
  nextpid = nextpid + 1;
  release(&pid_lock);
  // printf("process %d  released %s lock\n", pid, pid_lock.name);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    // if(p->pid != 0)
      // printf("process %d  acquired %s lock\n", p->pid, p->lock.name);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
      // if(p->pid != 0)      
        // printf("process %d  released %s lock\n", p->pid, p->lock.name);

    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;

  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  struct storedpage* sp;
  int i = 0;
  for(sp = p->storedpages; sp < &p->storedpages[MAX_TOTAL_PAGES]; sp++){
    sp->in_use = 0;
    sp->page_address = 0;
    sp->file_offset = PGSIZE*i;
    i++;
  }
  struct page_access_info* pi;
  p->page_turn = 0;
  for(pi=p->ram_pages; pi<&p->ram_pages[MAX_PSYC_PAGES]; pi++){
    pi->loaded_at = 0;
    pi->access_counter = 0;
    if (SELECTION == LAPA)
      pi->access_counter = 4294967295;  
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  
  p->pid = 0;
  p->pagetable = 0;
  p->sz = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
  p->page_turn = 0;
}

// Create a user page table for a given process,
// with no user memory, but with trampoline pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe just below TRAMPOLINE, for trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// od -t xC initcode
uchar initcode[] = {
  0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
  0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
  0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
  0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
  0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
  0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;
  
  // allocate one user page and copy init's instructions
  // and data into it.
  uvminit(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  p->trapframe->epc = 0;      // user program counter
  p->trapframe->sp = PGSIZE;  // user stack pointer

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if((sz = uvmalloc(p->pagetable, sz, sz + n)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

char buffer[PGSIZE];

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();
  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    // printf("process %d released %s lock\n", np->pid, np->lock.name);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  release(&np->lock);
  createSwapFile(np);
  acquire(&np->lock);

  struct storedpage* sp;
  struct storedpage* nsp = np->storedpages;

  for(sp = p->storedpages; sp < &p->storedpages[MAX_TOTAL_PAGES]; sp++){
    if(sp->in_use){
      if(readFromSwapFile(p,buffer,sp->file_offset,PGSIZE) < 0)
        return -1;
    
      release(&np->lock);
      // if(p->pid != 0)
        // printf("process %d released %s lock\n", np->pid, np->lock.name);
      if(writeToSwapFile(np,buffer,sp->file_offset,PGSIZE) < 0)
        return -1;
      acquire(&np->lock);
      // if(p->pid != 0)
        // printf("process %d acuired %s lock\n", np->pid, np->lock.name);
      nsp->page_address = sp->page_address;
      nsp->in_use = sp->in_use;
    }
    nsp++;
  }

  struct page_access_info* pi;
  struct page_access_info* npi = np->ram_pages;

  for(pi = p->ram_pages; pi < &p->ram_pages[MAX_PSYC_PAGES]; pi++){
    if(pi->in_use){
      npi->page_address = pi->page_address;
      npi->in_use = pi->in_use;
    }
    npi++;
  }
  
  pid = np->pid;
  release(&np->lock);
  // printf("process %d released %s lock\n", np->pid, np->lock.name);
  acquire(&wait_lock);
  // printf("process %d acuired %s lock\n", p->pid, wait_lock.name);

  np->parent = p;
  release(&wait_lock);
  // printf("process %d released %s lock\n", p->pid, wait_lock.name);

  acquire(&np->lock);
  // printf("process %d acuired %s lock\n", np->pid, np->lock.name);
  np->state = RUNNABLE;
  release(&np->lock);
  // printf("process %d released %s lock\n", np->pid, np->lock.name);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
exit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }
  removeSwapFile(p);

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  // printf("process %d released %s lock\n", p->pid, wait_lock.name);
  acquire(&wait_lock);
  // printf("process %d released %s lock\n", p->pid, wait_lock.name);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);
  
  acquire(&p->lock);
  // printf("process %d acuired %s lock\n", p->pid, p->lock.name);

  p->xstate = status;
  p->state = ZOMBIE;

  release(&wait_lock);
  // printf("process %d released %s lock\n", p->pid, wait_lock.name);


  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(uint64 addr)
{
  struct proc *np;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(np = proc; np < &proc[NPROC]; np++){
      if(np->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&np->lock);
        // if(p->pid != 0)
          // printf("process %d acquired %s lock\n", np->pid, np->lock.name);

        havekids = 1;
        if(np->state == ZOMBIE){
          // Found one.
          pid = np->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&np->xstate,
                                  sizeof(np->xstate)) < 0) {
            release(&np->lock);
            // if(p->pid != 0)      
              // printf("process %d released %s lock\n", np->pid, np->lock.name);
            release(&wait_lock);
            // if(p->pid != 0)          
              // printf("process %d released %s lock\n", p->pid, wait_lock.name);
            return -1;
          }
          freeproc(np);
          release(&np->lock);
          // if(p->pid != 0)
            // printf("process %d released %s lock\n", np->pid, np->lock.name);
          release(&wait_lock);
          // if(p->pid != 0)
            // printf("process %d released %s lock\n", p->pid, wait_lock.name);

          return pid;
        }
        release(&np->lock);
        // printf("process %d released %s lock\n", np->pid, np->lock.name);

      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || p->killed){
      release(&wait_lock);
      // if(p->pid != 0)
        // printf("process %d released %s lock\n", p->pid, wait_lock.name);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  
  c->proc = 0;
  for(;;){
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();

    for(p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      // if(p->pid !=0)
        // printf("process %d acquired %s lock\n", p->pid, p->lock.name);
      if(p->state == RUNNABLE) {
        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        p->state = RUNNING;
        c->proc = p;
        swtch(&c->context, &p->context);

        update_access_counters(p);
        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
      }
      release(&p->lock);
      // if(p->pid !=0)
        // printf("process %d released %s lock\n", p->pid, p->lock.name);

    }
  }
}
// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena anschedulerd proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();
  // printf("process %d on cpu %d has %d noff\n", p->pid,cpuid(), mycpu()->noff);

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU foschedulerr one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  // if(p->pid !=0)
    // printf("process %d acquired %s lock\n", p->pid, p->lock.name);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
  // if(p->pid !=0)
    // printf("process %d released %s lock\n", p->pid, p->lock.name);

}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  static int first = 1;

  // Still holding p->lock from scheduler.
  release(&myproc()->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular processreadFromSwapFile (e.g., because it calls sleep), and thus cannot
    // be run from main().
    first = 0;
    fsinit(ROOTDEV);
  }

  usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // Must acquire p->lreadFromSwapFileock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock);  //DOC: sleeplock1
  // printf("process %d acquired %s lock\n", p->pid, p->lock.name);
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  // printf("process %d released %s lock\n", p->pid, p->lock.name);
  acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    if(p != myproc()){
      acquire(&p->lock);
      // if(p->pid != 0)
        // printf("process %d acquired %s lock\n", p->pid, p->lock.name);
      if(p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
      }
      release(&p->lock);
      // if(p->pid != 0)
        // printf("process %d released %s lock\n", p->pid, p->lock.name);
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kill(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    // if(p->pid != 0)
      // printf("process %d acquired %s lock\n", p->pid, p->lock.name);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      // if(p->pid != 0)      
        // printf("process %d released %s lock\n", p->pid, p->lock.name);
      return 0;
    }
    release(&p->lock);
    // if(p->pid != 0)
      // printf("process %d released %s lock\n", p->pid, p->lock.name);
  }
  return -1;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}

int
store_page(pte_t *pte, uint64 page_address){
  struct page_access_info* pi;
  struct proc *p = myproc();
  struct storedpage *sp = get_free_storedpage();

  uint64 pa = PTE2PA(*pte);

  if(!sp || !pa || writeToSwapFile(p, (char*)pa, sp->file_offset, PGSIZE) < 0)
    return -1;
  
  sp->in_use = 1;
  sp->page_address = page_address;
  *pte |= PTE_PG;
  *pte &= ~PTE_V;

  for(pi=p->ram_pages; pi<&p->ram_pages[MAX_PSYC_PAGES]; pi++){
    if(pi->page_address == page_address)
      pi->in_use = 0;
  }
  
  kfree((void*)pa);

  return 0;
}

// load page which va belongs to from disk to pa
int
load_page(uint64 va){
  struct proc *p = myproc();
  struct storedpage *sp = get_wanted_storedpage(va);
  pte_t *pte;

  if((pte = walk(p->pagetable,va,0)) == 0)
    return -1;

  uint64 pa;
  if((pa = (uint64) kalloc()) == 0){
    return -1;
  }

  if(!sp || readFromSwapFile(p, (char*)pa, sp->file_offset, PGSIZE) < 0)
    return -1;
  
  sp->in_use = 0;
  sp->page_address = 0;

  if(mappages(p->pagetable, PGROUNDDOWN(va), PGSIZE, pa, PTE_FLAGS(*pte)) != 0){
    printf("failed to mappages, gonna free pa\n");
    kfree((void*)pa);
    return -1;
  }

  struct page_access_info *pi;
  for(pi=p->ram_pages; pi<&p->ram_pages[MAX_PSYC_PAGES]; pi++){
    if(!pi->in_use){
      pi->in_use = 1;
      pi->page_address = P_LEVELS_ADDRESS(va);
      pi->loaded_at = get_next_turn(p);
      if(SELECTION == LAPA)
        pi->access_counter = 4294967295;
      else
        pi->access_counter = 0;
      break;
    }
  }

  *pte &= ~PTE_PG;

  return 0;
}

struct storedpage*
get_free_storedpage(void){
  struct proc *p = myproc();
  struct storedpage* sp;
  for(sp = p->storedpages; sp < &p->storedpages[MAX_TOTAL_PAGES]; sp++){
    if(!sp->in_use){
      return sp;
    }
  }
  return 0;
}

struct storedpage*
get_wanted_storedpage(uint64 va){
  struct proc *p = myproc();
  struct storedpage* sp;
  uint64 page_address = P_LEVELS_ADDRESS(va);

  for(sp = p->storedpages; sp < &p->storedpages[MAX_TOTAL_PAGES]; sp++){
    if(sp->page_address == page_address && sp->in_use){
      // printf("found wanted page in storage, va = %p\n",va);
      return sp;
    }
  }
  // printf("didnt find the wanted page in storage\n");
  return 0;
}

void
update_access_counters(struct proc *p){
  struct page_access_info *pi;
  for(pi=p->ram_pages; pi<&p->ram_pages[MAX_PSYC_PAGES]; pi++){
    pi->access_counter = pi->access_counter >> 1;
    pte_t *pte = walk(p->pagetable,pi->page_address,0); 
    //if page is valid and was accessed
    if(*pte & PTE_V && *pte & PTE_A){
      pi->access_counter |= 1 << 31;
      *pte &= ~PTE_A;
    }
  }
}

int
count_ones(uint accesses){
  int i;
  int counter = 0;
  uint mask = 1;
  for(i=0; i<32; i++){
    if(mask & accesses)
      counter++ ;
    mask *=2;
  }
  return counter;
}

uint64 get_next_turn(struct proc* p){
  uint64 next_turn = p->page_turn;
  p->page_turn++;
  return next_turn;
} 

uint64
find_nfu(void){
  struct proc *p = myproc();
  uint64 _min = 18446744073709551615UL; 
  struct page_access_info *pi;
  struct page_access_info *min_pi = 0;
  for(pi=p->ram_pages; pi<&p->ram_pages[MAX_PSYC_PAGES]; pi++){
    if(pi->in_use && pi->access_counter < _min){
      _min = pi->access_counter;
      min_pi = pi;
    }
  }

  return min_pi->page_address;
}

uint64 
find_scfifo(void){
  struct proc *p = myproc();
  struct page_access_info *pi;
  uint64 _min;
  struct page_access_info *min_pi;
  pte_t* pte;
  printf("Process %d executing find_scfifo\n", p->pid);
  int looper = 0;
  while(1){
    looper++;
    _min = 18446744073709551615UL;
    min_pi = 0;

    for(pi=p->ram_pages; pi<&p->ram_pages[MAX_PSYC_PAGES]; pi++){
      if(pi->in_use && pi->loaded_at < _min){
        _min = pi->loaded_at;
        min_pi = pi;
      }
    }
    pte = walk(p->pagetable,min_pi->page_address,0);
    
    if(*pte & PTE_A){
      min_pi->loaded_at = get_next_turn(p);
      *pte &= ~PTE_A;
    } 
    else{
      break;
    }

  }
  return min_pi->page_address;
}

uint64
find_lapa(void){
  struct proc *p = myproc();
  uint _min = 32;
  struct page_access_info *pi;
  struct page_access_info *min_pi = 0;
  for(pi=p->ram_pages; pi<&p->ram_pages[MAX_PSYC_PAGES]; pi++){
    if(pi->in_use && count_ones(pi->access_counter) < _min){
      _min = count_ones(pi->access_counter);
      min_pi = pi;
    }
    else if(pi->in_use && count_ones(pi->access_counter) == _min){
      if(!min_pi || pi->access_counter < min_pi->access_counter){
        _min = count_ones(pi->access_counter);
        min_pi = pi;
      }
    }
  }
  return min_pi->page_address;
}

pte_t*
find_page_to_store(uint64* page_address){
  struct proc *p = myproc();
  switch(SELECTION){
    case NFUA:
      *page_address = find_nfu();
      return walk(p->pagetable,*page_address,0);
    case LAPA:
      *page_address = find_lapa();
      printf("p %d Found page using LAPA!  calling walk..\n",p->pid);
      return walk(p->pagetable,*page_address,0);
    case SCFIFO:
      *page_address = find_scfifo();
      printf("p %d Found page using SCFIFO! calling walk..\n",p->pid);      
      return walk(p->pagetable,*page_address,0);
  }
  return 0;
}
