#include "types.h"
#include "stat.h"
#include "user.h"
#include "ProcessInfo.h"
#include "param.h"

int
main(void)
{
  enum procstate { UNUSED, EMBRYO, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct ProcessInfo* processInfoTable = (struct ProcessInfo*) malloc((uint)(NPROC * sizeof(struct ProcessInfo)));
  int numberOfCurrentProcesses = getprocs(processInfoTable);
  int i;
  int lineNumber;
  for (i = 0, lineNumber = 1; i < numberOfCurrentProcesses; i++, lineNumber++)
    printf(1, "%d  %d  %s  %d  %s\n", lineNumber, processInfoTable[i].ppid, states[processInfoTable[i].state], processInfoTable[i].sz, processInfoTable[i].name);
  free((void *)processInfoTable);
  processInfoTable = NULL;
  exit();
}