#include "types.h"
#include "user.h"

#define PGSIZE (4096)

// Creates a new thread by first allocating a page-aligned user stack, then calling the clone syscall.  Returns the pid of the new thread.
int
thread_create(void (*start_routine)(void*), void* arg)
{
  void *stack = malloc(2 * PGSIZE);
  if ((uint)stack % PGSIZE != 0) stack += PGSIZE - (uint)stack % PGSIZE;
  return clone(start_routine, arg, stack);
}

// Calls join to wait for the thread specified by pid to complete.  Cleans up the completed thread's user stack.
int
thread_join(int pid)
{
  return join(pid);
}

// void lock_acquire(lock_t* lock);
// void lock_release(lock_t* lock);
// void lock_init(lock_t* lock);
// void cv_wait(cond_t* conditionVariable, lock_t* lock);
// void cv_signal(cond_t* conditionVariable);