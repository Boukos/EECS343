#include "types.h"
#include "user.h"
#include "fcntl.h"
#include "hw6-common.h"

int main() {
  int fd, r;
  char buf[1024];

  printf(1, "Homework 6 -- Permission Test\n");

  // WRONLY
  fd = open("testperm", O_WRONLY | O_CREATE);
  if (fd < 0)
    printf(1, "open() fail");

  // tagFile in WRONLY
  r = tagFile(fd, "test", "TEST!", strlen("TEST!") + 1);
  if (r < 0)
    printf(1, "tagFile() fail");

  // getFileTag in WRONLY
  r = getFileTag(fd, "test", buf, 1024);
  if (r >= 0)
    printf(1, "getFileTag() fail -- should return -1");

  // removeFileTag in WRONLY
  r = removeFileTag(fd, "test");
  if (r < 0)
    printf(1, "removeFileTag() fail");

  // Used for RDONLY tests
  r = tagFile(fd, "test", "TEST@", strlen("TEST@") + 1);
  if (r < 0)
    printf(1, "tagFile() fail");

  r = close(fd);
  if (r < 0)
    printf(1, "close() fail");

  // RDONLY
  fd = open("testperm", O_RDONLY);
  if (fd < 0)
    printf(1, "open() fail");

  // getFileTag in RDONLY
  r = getFileTag(fd, "test", buf, 1024);
  if (r < 0)
    printf(1, "getFileTag() fail");
  if (strcmp(buf, "TEST@") != 0)
    printf(1, "getFileTag() wrong");

  // tagFile in RDONLY
  r = tagFile(fd, "test", "TEST!", strlen("TEST!") + 1);
  if (r >= 0)
    printf(1, "tagFile() fail -- should return -1");

  // removeFileTag in RDONLY
  r = removeFileTag(fd, "test");
  if (r >= 0)
    printf(1, "removeFileTag() fail -- should return -1");

  printf(1, "hw6-test-permission succeeded\n");
  exit();
}
