/* call tagFile to tag a file.  Call getFileTag to read the tag of that file. */
#include "types.h"
#include "user.h"

#define O_RDONLY  0x000
#define O_WRONLY  0x001
#define O_RDWR    0x002
#define O_CREATE  0x200

#undef NULL
#define NULL ((void*)0)

int ppid;
volatile int global = 1;

#define assert(x) if (x) {} else { \
   printf(1, "%s: %d ", __FILE__, __LINE__); \
   printf(1, "assert failed (%s)\n", # x); \
   printf(1, "TEST FAILED\n"); \
   kill(ppid); \
   exit(); \
}

int
main(int argc, char *argv[])
{
   ppid = getpid();
   int fd = open("ls", O_RDWR);
   printf(1, "fd of ls: %d\n", fd);
   int res0 = tagFile(fd, "M0", "CS", 2);
   assert(res0 > 0);
   char buf[2];
   int res1 = getFileTag(fd, "M0", buf, 2);
   assert(res1 == 2);

   char buf1[1];
   int res2 = getFileTag(fd, "M0", buf1, 1);
   assert(res2 == 2);
   
   char buf2[2];
   int res3 = getFileTag(fd, "M1", buf2, 2);
   assert(res3 < 0);

   close(fd);

   int i;
   char *val = "CS";
   for(i = 0; i < 2; i++){
      char v_actual = buf[i];
      char v_expected = val[i];
      assert(v_actual == v_expected);
   }

   char buf3[2];
   int res4 = getFileTag(fd, "M0", buf3, 2);
   assert(res4 < 0);

   printf(1, "TEST PASSED\n");
   exit();
}