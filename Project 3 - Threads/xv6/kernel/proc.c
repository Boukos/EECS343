#include "types.h"
#include "defs.h"
#include "param.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;
  release(&ptable.lock);
  return 0;

found:
  p->isThread = 0;
  p->state = EMBRYO;
  p->pid = nextpid++;
  release(&ptable.lock);
  
  initlock(&p->lock, "proc"); // 1) clone: Requirement 12

  // Allocate kernel stack if possible.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;
  
  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;
  
  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];
  
  p = allocproc();
  acquire(&ptable.lock);
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;
  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  acquire(&proc->parent->lock);
  uint sz;
  sz = proc->sz;
  if(n > 0){
    if((sz = allocuvm(proc->pgdir, sz, sz + n)) == 0) {
      release(&proc->parent->lock);
      return -1;
    }
  } else if(n < 0){
    if((sz = deallocuvm(proc->pgdir, sz, sz + n)) == 0) {
      release(&proc->parent->lock);
      return -1;
    }
  }
  proc->sz = sz;
  switchuvm(proc);
  release(&proc->parent->lock);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;

  // Allocate process.
  if((np = allocproc()) == 0)
    return -1;

  // Copy process state from p.
  if((np->pgdir = copyuvm(proc->pgdir, proc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = proc->sz;
  np->parent = proc;
  *np->tf = *proc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(proc->ofile[i])
      np->ofile[i] = filedup(proc->ofile[i]);
  np->cwd = idup(proc->cwd);
 
  pid = np->pid;
  np->state = RUNNABLE;
  safestrcpy(np->name, proc->name, sizeof(proc->name));
  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *p;
  int fd;

  if(proc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(proc->ofile[fd]){
      fileclose(proc->ofile[fd]);
      proc->ofile[fd] = 0;
    }
  }

  iput(proc->cwd);
  proc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(proc->parent);

  // Pass abandoned children to init.
  // 1) clone: Requirement 11
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(proc->isThread == 0 && p->parent == proc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  proc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
/**
3) side effects

Requirements (and hints)

Here is a list of specific requirements related to the wait syscall:

01 The wait syscall must only wait for child processes, not child threads.
**/
int
wait(void)
{
  struct proc *p;
  int havekids, pid;

  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for zombie children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->isThread == 1 || p->parent != proc) // 3) side effects: Requirement 01
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->state = UNUSED;
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || proc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(proc, &ptable.lock);  //DOC: wait-sleep
  }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;

  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      proc = p;
      switchuvm(p);
      p->state = RUNNING;
      swtch(&cpu->scheduler, proc->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      proc = 0;
    }
    release(&ptable.lock);

  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state.
void
sched(void)
{
  int intena;

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(cpu->ncli != 1)
    panic("sched locks");
  if(proc->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = cpu->intena;
  swtch(&proc->context, cpu->scheduler);
  cpu->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  proc->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);
  
  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  if(proc == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }

  // Go to sleep.
  proc->chan = chan;
  proc->state = SLEEPING;
  sched();

  // Tidy up.
  proc->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];
  
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

/**
1) clone

Requirements (and hints)

Here is a list of specific requirements related to the clone syscall:

01. The clone syscall must use the exact function signature that we have provided above.

02. The clone syscall must create a new thread of execution with the same address space as the parent. (?)

03. The new thread must get a copy of the parent's file descriptors. (?)
    Hint: see fork.

04. The new thread must begin execution in the function pointed to by fcn.
    Hint: what register keeps track of the current instruction being executed?

05. The new thread must use the page pointed to by stack as its user stack. (?)

06. The pointer arg will be passed to the function (specified by fcn) as an argument. (?)
    Hint: review the calling convention.  When a function is called, where on the stack will it expect to find an argument that was passed in?

07. The function (specified by fcn) will have a fake return address of 0xffffffff.
    Hint: where on the stack does a function expect to find the return address?

08. Each thread will have a unique pid.

09. The parent field of the thread's struct proc will point to the parent process (aka the main thread).  If a thread spawns more threads, their parent field will point to the main thread (not to the thread that spawned them).

10. The clone syscall must return the pid of the new thread to the caller.  However, if a bad argument is passed to clone, or generally if anything goes wrong, clone must return -1 to the caller to indicate failure.
    Hint: clone should check that the stack argument it receives is page-aligned and that the full page has been allocated to the process.

11. In a multi-threaded process, when a thread exits, all other threads must be able to continue running.  However, when the main thread (parent process) exits, all of its children threads must be killed and cleaned up.
    Hint: the exit() code will have to change.  Also, it may be helpful to add a flag to the struct proc indicating whether this entry is a child thread or the main thread.

12. In a multi-threaded process, multiple threads must be able to grow the address space without causing race-related errors. 
    Hint: walk through the code path for the sbrk syscall.  You will need to use a lock in there somewhere.
**/

int
clone(void(*fcn)(void*), void* arg, void* stack) // Prequirement 01
{
  if ((uint) stack % PGSIZE != 0) return -1; // Prequirement 10

  int i, tid;
  struct proc *thread, *p;
  
  if ((thread = allocproc()) == 0) return -1; // Prequirement 08

  *(thread->tf) = *(proc->tf);
  thread->isThread = 1; // Prequirement 11
  thread->pgdir = proc->pgdir; // Prequirement 02
  thread->sz = proc->sz;

  // BEGIN: Prequirement 09
  for (p = proc; p->isThread == 1; p = p->parent) ;
  thread->parent = p;
  // thread->parentlock = &(p->lock); // 1) clone: Requirement 12
  // while (thread->parent->isThread == 1) thread->parent = thread->parent->parent;
  // cprintf("thread->parent = %d\tproc = %d\n", thread->parent, proc);
  // cprintf("thread->parent->isThread = %d\tproc->isThread = %d\n", thread->parent->isThread, proc->isThread);
  // END: Prequirement 09
  
  // BEGIN: Prequirement 05
  *((uint*)(stack + PGSIZE - 8)) = 0xffffffff; // Prequirement 07
  *((void**)(stack + PGSIZE - 4)) = arg;
  thread->tf->esp = (uint)stack;
  if (copyout(proc->pgdir, thread->tf->esp, (void*)stack, (uint)PGSIZE) < 0) return -1;
  thread->tf->esp = (uint)(stack + PGSIZE - 8);
  // END: Prequirement 05
  thread->tf->eip = (uint)fcn; // Prequirement 04
  // thread->tf->ebp = arg;
  thread->tf->eax = 0;

  // BEGIN: Prequirement 03
  for (i = 0; i < NOFILE; i++)
    if (proc->ofile[i])
      thread->ofile[i] = filedup(proc->ofile[i]);
  thread->cwd = idup(proc->cwd);
  // END: Prequirement 03
  
  tid = thread->pid;
  thread->state = RUNNABLE;
  safestrcpy(thread->name, proc->name, sizeof(proc->name));
  return tid;
}

// 2) join

// Requirements (and hints)

// Here is a list of specific requirements related to the join syscall:

// 01. The join syscall must use the exact function signature that we have provided above.

// 02. The join syscall must wait for the thread specified by pid to complete.

// 03. The join syscall must return the pid of the completed thread.  However, if the argument is invalid or generally if anything goes wrong, the syscall must return -1 to indicate failure.  Some notes on what constitutes an invalid argument:

// 04. Calling join on a main thread (a process) should result in a return value of -1.

// 05. Calling join on a thread belonging to a different thread group than the caller should also result in a return value of -1.

// 06. The join syscall must clean up the process table entry that was being used by the thread.
//     Hint: see the how wait does this.  However, be aware that wait cleans up some things that join must NOT clean up.

int
join(int pid) // Prequirement 01
{
  if (proc->pid == pid) return -1; // Prequirement 03

  struct proc *p;
  int havekids;

  acquire(&ptable.lock);
  for (;;) {
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
      if (p->pid == pid) {
        // BEGIN: Prequirement 04
        if (p->isThread == 0) {
          // cprintf("p->isThread == 0\n");
          release(&ptable.lock);
          return -1;
        }
        // END: Prequirement 04

        // BEGIN: Prequirement 05
        if (p->pgdir != proc->pgdir) {
          // cprintf("p->pgdir != proc->pgdir\n");
          release(&ptable.lock);
          return -1;          
        }
        // END: Prequirement 05

        // BEGIN: Prequirement 05
        if (proc->isThread == 1 && p->parent != proc->parent) {
          // cprintf("p->parent != proc->parent\n");
          // cprintf("p->parent = %d\tproc->parent = %d\n", p->parent, proc->parent);
          // cprintf("p->parent->isThread = %d\tproc->parent->isThread = %d\n", p->parent->isThread, proc->parent->isThread);
          release(&ptable.lock);
          return -1;           
        }
        // END: Prequirement 05

        havekids = 1;

        if (p->state == ZOMBIE) { // Prequirement 03
          kfree(p->kstack);
          p->kstack = 0;
          p->state = UNUSED;
          p->pid = 0;
          p->parent = 0;
          p->name[0] = 0;
          p->killed = 0;
          release(&ptable.lock);
          return pid;
        }      
      }
    }
    if (!havekids || proc->killed) {
      // cprintf("!havekids || proc->killed\n");
      release(&ptable.lock);
      return -1;         
    }
    sleep(proc, &ptable.lock);
  }
  return pid;
}

// BEGIN: Release the lock pointed to by lock and put the caller to sleep.  Assumes that lock is held when this is called.  When signaled, the thread awakens and reacquires the lock.
void
cv_wait(cond_t* conditionVariable, lock_t* lock)
{

}
// END: Release the lock pointed to by lock and put the caller to sleep.  Assumes that lock is held when this is called.  When signaled, the thread awakens and reacquires the lock.

// BEGIN: Wake the threads that are waiting on conditionVariable.
void
cv_signal(cond_t* conditionVariable)
{

}
// END: Wake the threads that are waiting on conditionVariable.