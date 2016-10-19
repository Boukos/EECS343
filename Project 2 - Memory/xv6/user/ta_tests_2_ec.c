#include "types.h"
#include "stat.h"
#include "user.h"

#define USERTOP 0xA0000
#define PGSIZE 4096

void
testPassed(void)
{
  printf(1, "....Passed\n");
}

void
testFailed(void)
{
  printf(1, "....FAILED\n");
}

void expectedVersusActualNumeric(char* name, int expected, int actual)
{
  printf(1, "      %s expected: %d, Actual: %d\n", name, expected, actual);
}

void
whenNotUsingSharedMemory_ProcessCanAllocateAllSpaceUpToUsertop()
{
  printf(1, "Test: whenNotUsingSharedMemory_ProcessCanAllocateAllSpaceUpToUsertop...");
  char* startAddress = sbrk(0);
  uint bytesRemaining = (160 * 4096) - (uint)startAddress - 4096;  // subtract 4096 for the 0th page
  char* sbrkResult = sbrk(bytesRemaining);

  if(sbrkResult == (char*)0xffffffff){
    testFailed();
    printf(1, "      Failed to use full user address space for normal memory.\n");
    expectedVersusActualNumeric("'sbrkResult'", 4096*159, (int)sbrkResult);
    return;
  }

  testPassed();
}

void
whenUsingOneSharedPage_ProcessCanAllocateAllRemainingSpace()
{
  printf(1, "Test: whenUsingOneSharedPage_ProcessCanAllocateAllRemainingSpace...");
  char* startAddress = sbrk(0);
  char* sharedPage = shmem_access(0);
  sharedPage = sharedPage + 0;
  uint bytesAvailable = (160 * 4096) - (uint)startAddress - 2 * 4096; // subtract 2 * 4096 for 0th page and shared page
  char* sbrkResult = sbrk(bytesAvailable);

  if(sbrkResult == (char*)0xffffffff) {
    testFailed();
    printf(1, "      Failed to use 1 page for shared mem and 158 for normal memory.\n");
    return;
  }

  testPassed();
}

void
whenUsingAllButTwoPagesForNormalMemory_ProcessCanAllocateTwoSharedPages()
{
  printf(1, "Test: whenUsingAllButTwoPagesForNormalMemory_ProcessCanAllocateTwoSharedPages...");
  char* startAddress = sbrk(0);
  uint bytesToAllocate = (160 * 4096) - (uint)startAddress - 3 * 4096; // subtract 3 * 4096 for 0th page and 2 shared pages
  char* sbrkResult = sbrk(bytesToAllocate);
  char* sharedPage0 = shmem_access(0);
  char* sharedPage4 = shmem_access(3);

  if(sbrkResult == (char*)0xffffffff) {
    testFailed();
    printf(1, "      Failed to allocate normal memory.\n");
    return;
  }

  if(sharedPage0 == NULL) {
    testFailed();
    printf(1, "      Failed to allocate first shared page.\n");
    return;
  }

  if(sharedPage4 == NULL) {
    testFailed();
    printf(1, "      Failed to allocate second shared page.\n");
    return;
  }

  testPassed();
  
}

void
whenSharingAPage_ParentSeesChangesMadeByChild()
{
  printf(1, "Test: whenSharingAPage_ParentSeesChangesMadeByChild...");
  char* sharedPage = shmem_access(0);
  sharedPage[0] = 42;

  int pid = fork();
  if(pid == 0){
    // in child
    char* childsSharedPage = shmem_access(0);
    childsSharedPage[0] = childsSharedPage[0] + 1;
    exit();
  } else {
    // in parent
    wait(); // wait for child to terminate
    if(sharedPage[0] == 43){
      testPassed();
    } else {
      testFailed();
      expectedVersusActualNumeric("'sharedPage[0]'", 43, sharedPage[0]);
    }
  }
}

int
main(void)
{
  int pid;

  // we fork then run each test in a child process to keep the main process
  // free of any shared memory
  pid = fork();
  if(pid == 0){
    whenNotUsingSharedMemory_ProcessCanAllocateAllSpaceUpToUsertop();
    exit();
  }
  wait();

  pid = fork();
  if(pid == 0){
    whenUsingOneSharedPage_ProcessCanAllocateAllRemainingSpace();
    exit();
  }
  wait();
  
  pid = fork();
  if(pid == 0){
    whenUsingAllButTwoPagesForNormalMemory_ProcessCanAllocateTwoSharedPages();
    exit();
  }
  wait();

  pid = fork();
  if(pid == 0){
    whenSharingAPage_ParentSeesChangesMadeByChild();
    exit();
  }
  wait();

  exit();
}
