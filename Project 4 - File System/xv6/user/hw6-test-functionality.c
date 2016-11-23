#include "types.h"
#include "user.h"
#include "fcntl.h"
#include "hw6-common.h"

int main() {
  int fd, r;
  char buf[1024];

  printf(1, "Homework 6 -- Functionality Test\n");

  fd = open("testfunc", O_RDWR | O_CREATE);
  if (fd < 0)
    printf(1, "open() fail");

  r = tagFile(fd, "test1", "TEST!", strlen("TEST!") + 1);
  if (r < 0)
    printf(1, "tagFile() fail");
	 
  r = tagFile(fd, "test2", "TEST2!", strlen("TEST2!") + 1);  // Mic
  if (r < 0) // Mic
    printf(1, "tagFile() fail"); // Mic

  r = getFileTag(fd, "test1", buf, 1024);
  if (r < 0)
    printf(1, "getFileTag() fail");
  if (strcmp(buf, "TEST!") != 0)
    printf(1, "getFileTag() wrong");
	
  r = getFileTag(fd, "test2", buf, 1024); // Mic
  if (r < 0) // Mic
    printf(1, "getFileTag() fail"); // Mic
  if (strcmp(buf, "TEST2!") != 0) // Mic
    printf(1, "getFileTag() wrong"); // Mic

  r = removeFileTag(fd, "test1");
  if (r < 0)
    printf(1, "removeFileTag() fail");

  r = getFileTag(fd, "test1", buf, 1024);
  if (r >= 0)
    printf(1, "getFileTag() fail -- should return -1");

  r = tagFile(fd, "test2", "TEST@", strlen("TEST@") + 1);
  if (r < 0)
    printf(1, "tagFile() fail");

  r = close(fd);
  if (r < 0)
    printf(1, "close() fail");

  fd = open("testfunc", O_RDWR);
  if (fd < 0)
    printf(1, "open() fail");

  r = getFileTag(fd, "test1", buf, 1024);
  if (r >= 0)
    printf(1, "getFileTag() fail -- should return -1");

  r = getFileTag(fd, "test2", buf, 1024);
  if (r < 0)
    printf(1, "getFileTag() fail");
  if (strcmp(buf, "TEST@") != 0)
    printf(1, "getFileTag() wrong");

  r = close(fd);
  if (r < 0)
    printf(1, "close() fail");

  printf(1, "hw6-test-functionality succeeded\n");
  exit();
}
