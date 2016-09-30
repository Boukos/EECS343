#ifndef _ProcessInfo_H_
#define _ProcessInfo_H_
struct ProcessInfo {
  int pid; // process id
  int ppid; // parent pid
  int state; // state
  uint sz; // size in bytes
  char name[16]; // name of process
};
#endif // _ProcessInfo_H_