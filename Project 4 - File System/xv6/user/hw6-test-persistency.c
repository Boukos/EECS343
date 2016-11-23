#include "types.h"
#include "user.h"
#include "fcntl.h"
#include "hw6-common.h"

int main() {
  int fd, r;
  char buf[1024];

  printf(1, "Homework 6 -- Persistency Test\n");

  fd = open("testper", O_RDONLY);
  if (fd < 0)
    printf(1, "open() fail");

  r = getFileTag(fd, "test1", buf, 1024);
  if (r < 0)
    printf(1, "getFileTag() fail");
  if (strcmp(buf, "TEST!") != 0)
    printf(1, "getFileTag() wrong");

  r = close(fd);
  if (r < 0)
    printf(1, "close() fail");

  printf(1, "hw6-test-persistency succeeded\n");
  exit();
}
