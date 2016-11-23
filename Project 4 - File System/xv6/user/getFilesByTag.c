#include "types.h"
#include "user.h"
// make sure that struct Key is included via either types.h or user.h above

#define O_RDONLY 0x000
#define O_WRONLY 0x001
#define O_RDWR 0x002
#define O_CREATE 0x200

int ppid;

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
    int fd, res;
    fd = open("ls", O_RDWR);
    char * key = "type1";
    char * val = "utility1";
    int len = 8;
    res = tagFile(fd, key, val, len);  
    assert(res > 0);
    key = "type2";
    val = "utility2";
    res = tagFile(fd, key, val, len);
    assert(res > 0);
    key = "type3";
    val = "utility3";
    res = tagFile(fd, key, val, len);
    assert(res > 0);
    close(fd);
  
    fd = open("README", O_RDWR);
    key = "type1";
    val = "utility1";
    res = tagFile(fd, key, val, len);
    assert(res > 0);
    close(fd);

    char results[20];

    int files = getFilesByTag(key, val, 8, results, 20);
    assert(files == 2);
    assert(results[0] == 'l');
    assert(results[1] == 's');
    assert(results[2] == NULL);
    assert(results[3] == 'R');
    assert(results[4] == 'E');
    assert(results[5] == 'A');
    assert(results[6] == 'D');
    assert(results[7] == 'M');
    assert(results[8] == 'E');
    printf(1, "getFilesByTag test passed\n");
    exit();
}
        

