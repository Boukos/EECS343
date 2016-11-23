#include "types.h"
#include "user.h"
#include "fcntl.h"
#include "hw6-common.h"

int main() {
  int fd, r;

  printf(1, "Homework 6 -- Persistency Test Preparation\n");

  /* simply create and tag a file "testper" then exit,
   * hw6-test-per will test if the tagging succeeded */

  fd = open("testper", O_WRONLY | O_CREATE);
  if (fd < 0)
    printf(1, "open() fail");

  r = tagFile(fd, "test1", "TEST!", strlen("TEST!") + 1);
  if (r < 0)
    printf(1, "tagFile() fail");

  r = close(fd);
  if (r < 0)
    printf(1, "close() fail");

  printf(1, "hw6-test-prepare succeeded\n");
  exit();
}
