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
   int fd = open("ls", O_RDWR);
   // tagFile(fd, "type", "utility", 7);
   // tagFile(fd, "type", "xmanxma", 7);
   tagFile(fd, "type", "yeildxxx", 8);
   // tagFile(fd, "language", "English", 7);
   // tagFile(fd, "language", "Java", 4);
   // tagFile(fd, "school", "Stanford", 8);
   // tagFile(fd, "school", "MIT", 3);
   // tagFile(fd, "Game", "NBA", 3);
   // tagFile(fd, "Game", "CUBA", 4);
   close(fd);

   int fd1 = open("README", O_RDWR);
   tagFile(fd, "type", "yeildxxx", 8);
   close(fd1);

   // fd = open("ls", O_RDONLY);
   // char buf0[8];
   // char buf1[4];
   // char buf2[3];
   // char buf3[4];
   // getFileTag(fd, "type", buf0, 8);
   // printf(1, "buf0 = %s\n", buf0);
   // getFileTag(fd, "language", buf1, 4);
   // printf(1, "buf1 = %s\n", buf1);
   // getFileTag(fd, "school", buf2, 3);
   // printf(1, "buf2 = %s\n", buf2);
   // getFileTag(fd, "Game", buf3, 4);
   // printf(1, "buf3 = %s\n", buf3);
   // close(fd);

   char results[10];
   int cnt = getFilesByTag("type", "yeildxxx", 8, results, 10);
   printf(1, "cnt = %d\n", cnt);
   printf(1, "results = %s\n", results);
   printf(1, "strlen(results) = %d\n", strlen(results));
   printf(1, "TEST PASSED\n");
   exit();
}