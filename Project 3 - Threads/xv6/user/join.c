/* Call clone to create a new thread.  Call join to wait for that new thread to complete.  join should return the pid of the completed thread and the changes made by the thread should now be seen by the original process. */
#include "types.h"
#include "user.h"

#undef NULL
#define NULL ((void*)0)

#define PGSIZE (4096)

int ppid;
int global = 1;

#define assert(x) if (x) {} else { \
   printf(1, "%s: %d ", __FILE__, __LINE__); \
   printf(1, "assert failed (%s)\n", # x); \
   printf(1, "TEST FAILED\n"); \
   kill(ppid); \
   exit(); \
}

void worker(void *arg_ptr);

int
main(int argc, char *argv[])
{
   ppid = getpid();

   void *stack = malloc(PGSIZE*2);
   assert(stack != NULL);
   if((uint)stack % PGSIZE)
     stack = stack + (PGSIZE - (uint)stack % PGSIZE);

   int arg = 42;
   int new_thread_pid = clone(worker, &arg, stack);
   // printf(1, "return value = %d\n", *(uint*)(stack + PGSIZE - 8));
   assert(new_thread_pid > 0);
   printf(1, "stack = %d\n", (int)stack);
   printf(1, "ustack = %d\n", find_ustack(new_thread_pid));
   int ustack = find_ustack(new_thread_pid);
   assert((int)stack == ustack);
   free((void*)ustack);
   int join_pid = join(new_thread_pid);
   printf(1, "return value = %d\n", *(uint*)(stack + PGSIZE - 8));
   assert(join_pid == new_thread_pid);
   assert(global == 2);

   printf(1, "TEST PASSED\n");
   exit();
}

void
worker(void *arg_ptr) {
   int arg = *(int*)arg_ptr;
   assert(arg == 42);
   assert(global == 1);
   global++;
   exit();
}