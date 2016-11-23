#include "types.h"
#include "x86.h"
#include "fs.h"

void*
memset(void *dst, int c, uint n)
{
  stosb(dst, c, n);
  return dst;
}

int
memcmp(const void *v1, const void *v2, uint n)
{
  const uchar *s1, *s2;
  
  s1 = v1;
  s2 = v2;
  while(n-- > 0){
    if(*s1 != *s2)
      return *s1 - *s2;
    s1++, s2++;
  }

  return 0;
}

void*
memmove(void *dst, const void *src, uint n)
{
  const char *s;
  char *d;

  s = src;
  d = dst;
  if(s < d && s + n > d){
    s += n;
    d += n;
    while(n-- > 0)
      *--d = *--s;
  } else
    while(n-- > 0)
      *d++ = *s++;

  return dst;
}

// memcpy exists to placate GCC.  Use memmove.
void*
memcpy(void *dst, const void *src, uint n)
{
  return memmove(dst, src, n);
}

int
strncmp(const char *p, const char *q, uint n)
{
  while(n > 0 && *p && *p == *q)
    n--, p++, q++;
  if(n == 0)
    return 0;
  return (uchar)*p - (uchar)*q;
}

char*
strncpy(char *s, const char *t, int n)
{
  char *os;
  
  os = s;
  while(n-- > 0 && (*s++ = *t++) != 0)
    ;
  while(n-- > 0)
    *s++ = 0;
  return os;
}

// Like strncpy but guaranteed to NUL-terminate.
char*
safestrcpy(char *s, const char *t, int n)
{
  char *os;
  
  os = s;
  if(n <= 0)
    return os;
  while(--n > 0 && (*s++ = *t++) != 0)
    ;
  *s = 0;
  return os;
}

int
strlen(const char *s)
{
  int n;

  for(n = 0; s[n]; n++)
    ;
  return n;
}

int
searchKey(uchar* key, uchar* str)
{
  int i = 0, j = 0;
  int keyLength = strlen((char*)key);
  for (i = 0; i < BSIZE; i += 32) {
    j = 0;
    for ( ; j < 10 && i + j < BSIZE && key[j] && str[i + j] && key[j] == str[i + j]; j++) ;
    if (j == keyLength && !key[j] && !str[i + j]) return i + j - keyLength;
  }
  return -1;
}

int
searchEnd(uchar* str)
{
  int i = 0;
  for (i = 0; i < BSIZE && str[i]; i += 32) ;
  if (i == BSIZE) return -1;
  return i;
}