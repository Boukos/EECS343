// File system implementation.  Four layers:
//   + Blocks: allocator for raw disk blocks.
//   + Files: inode allocator, reading, writing, metadata.
//   + Directories: inode with special contents (list of other inodes!)
//   + Names: paths like /usr/rtm/xv6/fs.c for convenient naming.
//
// Disk layout is: superblock, inodes, block in-use bitmap, data blocks.
//
// This file contains the low-level file system manipulation 
// routines.  The (higher-level) system call implementations
// are in sysfile.c.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "mmu.h"
#include "proc.h"
#include "spinlock.h"
#include "buf.h"
#include "fs.h"
#include "file.h"

#define min(a, b) ((a) < (b) ? (a) : (b))
static void itrunc(struct inode*);

// Read the super block.
static void
readsb(int dev, struct superblock *sb)
{
  struct buf *bp;
  
  bp = bread(dev, 1);
  memmove(sb, bp->data, sizeof(*sb));
  brelse(bp);
}

// Zero a block.
static void
bzero(int dev, int bno)
{
  struct buf *bp;
  
  bp = bread(dev, bno);
  memset(bp->data, 0, BSIZE);
  bwrite(bp);
  brelse(bp);
}

// Blocks. 

// Allocate a disk block.
static uint
balloc(uint dev)
{
  int b, bi, m;
  struct buf *bp;
  struct superblock sb;

  bp = 0;
  readsb(dev, &sb);
  for(b = 0; b < sb.size; b += BPB){
    bp = bread(dev, BBLOCK(b, sb.ninodes));
    for(bi = 0; bi < BPB; bi++){
      m = 1 << (bi % 8);
      if((bp->data[bi/8] & m) == 0){  // Is block free?
        bp->data[bi/8] |= m;  // Mark block in use on disk.
        bwrite(bp);
        brelse(bp);
        return b + bi;
      }
    }
    brelse(bp);
  }
  panic("balloc: out of blocks");
}

// Free a disk block.
static void
bfree(int dev, uint b)
{
  struct buf *bp;
  struct superblock sb;
  int bi, m;

  bzero(dev, b);

  readsb(dev, &sb);
  bp = bread(dev, BBLOCK(b, sb.ninodes));
  bi = b % BPB;
  m = 1 << (bi % 8);
  if((bp->data[bi/8] & m) == 0)
    panic("freeing free block");
  bp->data[bi/8] &= ~m;  // Mark block free on disk.
  bwrite(bp);
  brelse(bp);
}

// Inodes.
//
// An inode is a single, unnamed file in the file system.
// The inode disk structure holds metadata (the type, device numbers,
// and data size) along with a list of blocks where the associated
// data can be found.
//
// The inodes are laid out sequentially on disk immediately after
// the superblock.  The kernel keeps a cache of the in-use
// on-disk structures to provide a place for synchronizing access
// to inodes shared between multiple processes.
// 
// ip->ref counts the number of pointer references to this cached
// inode; references are typically kept in struct file and in proc->cwd.
// When ip->ref falls to zero, the inode is no longer cached.
// It is an error to use an inode without holding a reference to it.
//
// Processes are only allowed to read and write inode
// metadata and contents when holding the inode's lock,
// represented by the I_BUSY flag in the in-memory copy.
// Because inode locks are held during disk accesses, 
// they are implemented using a flag rather than with
// spin locks.  Callers are responsible for locking
// inodes before passing them to routines in this file; leaving
// this responsibility with the caller makes it possible for them
// to create arbitrarily-sized atomic operations.
//
// To give maximum control over locking to the callers, 
// the routines in this file that return inode pointers 
// return pointers to *unlocked* inodes.  It is the callers'
// responsibility to lock them before using them.  A non-zero
// ip->ref keeps these unlocked inodes in the cache.

struct {
  struct spinlock lock;
  struct inode inode[NINODE];
} icache;

void
iinit(void)
{
  initlock(&icache.lock, "icache");
}

static struct inode* iget(uint dev, uint inum);

// Allocate a new inode with the given type on device dev.
struct inode*
ialloc(uint dev, short type)
{
  int inum;
  struct buf *bp;
  struct dinode *dip;
  struct superblock sb;

  readsb(dev, &sb);
  for(inum = 1; inum < sb.ninodes; inum++){  // loop over inode blocks
    bp = bread(dev, IBLOCK(inum));
    dip = (struct dinode*)bp->data + inum%IPB;
    if(dip->type == 0){  // a free inode
      memset(dip, 0, sizeof(*dip));
      dip->type = type;
      bwrite(bp);   // mark it allocated on the disk
      brelse(bp);
      return iget(dev, inum);
    }
    brelse(bp);
  }
  panic("ialloc: no inodes");
}

// Copy inode, which has changed, from memory to disk.
void
iupdate(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;

  bp = bread(ip->dev, IBLOCK(ip->inum));
  dip = (struct dinode*)bp->data + ip->inum%IPB;
  dip->type = ip->type;
  dip->major = ip->major;
  dip->minor = ip->minor;
  dip->nlink = ip->nlink;
  dip->size = ip->size;
  memmove(dip->addrs, ip->addrs, sizeof(ip->addrs));
  bwrite(bp);
  brelse(bp);
}

// Find the inode with number inum on device dev
// and return the in-memory copy.
static struct inode*
iget(uint dev, uint inum)
{
  struct inode *ip, *empty;

  acquire(&icache.lock);

  // Try for cached inode.
  empty = 0;
  for(ip = &icache.inode[0]; ip < &icache.inode[NINODE]; ip++){
    if(ip->ref > 0 && ip->dev == dev && ip->inum == inum){
      ip->ref++;
      release(&icache.lock);
      return ip;
    }
    if(empty == 0 && ip->ref == 0)    // Remember empty slot.
      empty = ip;
  }

  // Allocate fresh inode.
  if(empty == 0)
    panic("iget: no inodes");

  ip = empty;
  ip->dev = dev;
  ip->inum = inum;
  ip->ref = 1;
  ip->flags = 0;
  release(&icache.lock);

  return ip;
}

// Increment reference count for ip.
// Returns ip to enable ip = idup(ip1) idiom.
struct inode*
idup(struct inode *ip)
{
  acquire(&icache.lock);
  ip->ref++;
  release(&icache.lock);
  return ip;
}

// Lock the given inode.
void
ilock(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;

  if(ip == 0 || ip->ref < 1)
    panic("ilock");

  acquire(&icache.lock);
  while(ip->flags & I_BUSY)
    sleep(ip, &icache.lock);
  ip->flags |= I_BUSY;
  release(&icache.lock);

  if(!(ip->flags & I_VALID)){
    bp = bread(ip->dev, IBLOCK(ip->inum));
    dip = (struct dinode*)bp->data + ip->inum%IPB;
    ip->type = dip->type;
    ip->major = dip->major;
    ip->minor = dip->minor;
    ip->nlink = dip->nlink;
    ip->size = dip->size;
    memmove(ip->addrs, dip->addrs, sizeof(ip->addrs));
    brelse(bp);
    ip->flags |= I_VALID;
    if(ip->type == 0)
      panic("ilock: no type");
  }
}

// Unlock the given inode.
void
iunlock(struct inode *ip)
{
  if(ip == 0 || !(ip->flags & I_BUSY) || ip->ref < 1)
    panic("iunlock");

  acquire(&icache.lock);
  ip->flags &= ~I_BUSY;
  wakeup(ip);
  release(&icache.lock);
}

// Caller holds reference to unlocked ip.  Drop reference.
void
iput(struct inode *ip)
{
  acquire(&icache.lock);
  if(ip->ref == 1 && (ip->flags & I_VALID) && ip->nlink == 0){
    // inode is no longer used: truncate and free inode.
    if(ip->flags & I_BUSY)
      panic("iput busy");
    ip->flags |= I_BUSY;
    release(&icache.lock);
    itrunc(ip);
    ip->type = 0;
    iupdate(ip);
    acquire(&icache.lock);
    ip->flags = 0;
    wakeup(ip);
  }
  ip->ref--;
  release(&icache.lock);
}

// Common idiom: unlock, then put.
void
iunlockput(struct inode *ip)
{
  iunlock(ip);
  iput(ip);
}

// Inode contents
//
// The contents (data) associated with each inode is stored
// in a sequence of blocks on the disk.  The first NDIRECT blocks
// are listed in ip->addrs[].  The next NINDIRECT blocks are 
// listed in the block ip->addrs[NDIRECT].

// Return the disk block address of the nth block in inode ip.
// If there is no such block, bmap allocates one.
static uint
bmap(struct inode *ip, uint bn)
{
  uint addr, *a;
  struct buf *bp;

  if(bn < NDIRECT){
    if((addr = ip->addrs[bn]) == 0)
      ip->addrs[bn] = addr = balloc(ip->dev);
    return addr;
  }
  bn -= NDIRECT;

  if(bn < NINDIRECT){
    // Load indirect block, allocating if necessary.
    if((addr = ip->addrs[NDIRECT]) == 0)
      ip->addrs[NDIRECT] = addr = balloc(ip->dev);
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    if((addr = a[bn]) == 0){
      a[bn] = addr = balloc(ip->dev);
      bwrite(bp);
    }
    brelse(bp);
    return addr;
  }

  panic("bmap: out of range");
}

// Truncate inode (discard contents).
// Only called after the last dirent referring
// to this inode has been erased on disk.
static void
itrunc(struct inode *ip)
{
  int i, j;
  struct buf *bp;
  uint *a;

  for(i = 0; i < NDIRECT; i++){
    if(ip->addrs[i]){
      bfree(ip->dev, ip->addrs[i]);
      ip->addrs[i] = 0;
    }
  }
  
  if(ip->addrs[NDIRECT]){
    bp = bread(ip->dev, ip->addrs[NDIRECT]);
    a = (uint*)bp->data;
    for(j = 0; j < NINDIRECT; j++){
      if(a[j])
        bfree(ip->dev, a[j]);
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT]);
    ip->addrs[NDIRECT] = 0;
  }
  
  if (ip->tags) {
    bfree(ip->dev, ip->tags);
    ip->tags = 0;
  }

  ip->size = 0;
  iupdate(ip);
}

// Copy stat information from inode.
void
stati(struct inode *ip, struct stat *st)
{
  st->dev = ip->dev;
  st->ino = ip->inum;
  st->type = ip->type;
  st->nlink = ip->nlink;
  st->size = ip->size;
}

// Read data from inode.
int
readi(struct inode *ip, char *dst, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;

  if(ip->type == T_DEV){
    if(ip->major < 0 || ip->major >= NDEV || !devsw[ip->major].read)
      return -1;
    return devsw[ip->major].read(ip, dst, n);
  }

  if(off > ip->size || off + n < off)
    return -1;
  if(off + n > ip->size)
    n = ip->size - off;

  for(tot=0; tot<n; tot+=m, off+=m, dst+=m){
    bp = bread(ip->dev, bmap(ip, off/BSIZE));
    m = min(n - tot, BSIZE - off%BSIZE);
    memmove(dst, bp->data + off%BSIZE, m);
    brelse(bp);
  }
  return n;
}

// Write data to inode.
int
writei(struct inode *ip, char *src, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;

  if(ip->type == T_DEV){
    if(ip->major < 0 || ip->major >= NDEV || !devsw[ip->major].write)
      return -1;
    return devsw[ip->major].write(ip, src, n);
  }

  if(off > ip->size || off + n < off)
    return -1;
  if(off + n > MAXFILE*BSIZE)
    n = MAXFILE*BSIZE - off;

  for(tot=0; tot<n; tot+=m, off+=m, src+=m){
    bp = bread(ip->dev, bmap(ip, off/BSIZE));
    m = min(n - tot, BSIZE - off%BSIZE);
    memmove(bp->data + off%BSIZE, src, m);
    bwrite(bp);
    brelse(bp);
  }

  if(n > 0 && off > ip->size){
    ip->size = off;
    iupdate(ip);
  }
  return n;
}

// Directories

int
namecmp(const char *s, const char *t)
{
  return strncmp(s, t, DIRSIZ);
}

// Look for a directory entry in a directory.
// If found, set *poff to byte offset of entry.
// Caller must have already locked dp.
struct inode*
dirlookup(struct inode *dp, char *name, uint *poff)
{
  uint off, inum;
  struct buf *bp;
  struct dirent *de;

  if(dp->type != T_DIR)
    panic("dirlookup not DIR");

  for(off = 0; off < dp->size; off += BSIZE){
    bp = bread(dp->dev, bmap(dp, off / BSIZE));
    for(de = (struct dirent*)bp->data;
        de < (struct dirent*)(bp->data + BSIZE);
        de++){
      if(de->inum == 0)
        continue;
      if(namecmp(name, de->name) == 0){
        // entry matches path element
        if(poff)
          *poff = off + (uchar*)de - bp->data;
        inum = de->inum;
        brelse(bp);
        return iget(dp->dev, inum);
      }
    }
    brelse(bp);
  }
  return 0;
}

// Write a new directory entry (name, inum) into the directory dp.
int
dirlink(struct inode *dp, char *name, uint inum)
{
  int off;
  struct dirent de;
  struct inode *ip;

  // Check that name is not present.
  if((ip = dirlookup(dp, name, 0)) != 0){
    iput(ip);
    return -1;
  }

  // Look for an empty dirent.
  for(off = 0; off < dp->size; off += sizeof(de)){
    if(readi(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlink read");
    if(de.inum == 0)
      break;
  }

  strncpy(de.name, name, DIRSIZ);
  de.inum = inum;
  if(writei(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
    panic("dirlink");
  
  return 0;
}

// Paths

// Copy the next path element from path into name.
// Return a pointer to the element following the copied one.
// The returned path has no leading slashes,
// so the caller can check *path=='\0' to see if the name is the last one.
// If no name to remove, return 0.
//
// Examples:
//   skipelem("a/bb/c", name) = "bb/c", setting name = "a"
//   skipelem("///a//bb", name) = "bb", setting name = "a"
//   skipelem("a", name) = "", setting name = "a"
//   skipelem("", name) = skipelem("////", name) = 0
//
static char*
skipelem(char *path, char *name)
{
  char *s;
  int len;

  while(*path == '/')
    path++;
  if(*path == 0)
    return 0;
  s = path;
  while(*path != '/' && *path != 0)
    path++;
  len = path - s;
  if(len >= DIRSIZ)
    memmove(name, s, DIRSIZ);
  else {
    memmove(name, s, len);
    name[len] = 0;
  }
  while(*path == '/')
    path++;
  return path;
}

// Look up and return the inode for a path name.
// If parent != 0, return the inode for the parent and copy the final
// path element into name, which must have room for DIRSIZ bytes.
static struct inode*
namex(char *path, int nameiparent, char *name)
{
  struct inode *ip, *next;

  if(*path == '/')
    ip = iget(ROOTDEV, ROOTINO);
  else
    ip = idup(proc->cwd);

  while((path = skipelem(path, name)) != 0){
    ilock(ip);
    if(ip->type != T_DIR){
      iunlockput(ip);
      return 0;
    }
    if(nameiparent && *path == '\0'){
      // Stop one level early.
      iunlock(ip);
      return ip;
    }
    if((next = dirlookup(ip, name, 0)) == 0){
      iunlockput(ip);
      return 0;
    }
    iunlockput(ip);
    ip = next;
  }
  if(nameiparent){
    iput(ip);
    return 0;
  }
  return ip;
}

struct inode*
namei(char *path)
{
  char name[DIRSIZ];
  return namex(path, 0, name);
}

struct inode*
nameiparent(char *path, char *name)
{
  return namex(path, 1, name);
}

// 1) tagFile

// Requirements (and hints)

// Here is a list of specific requirements related to the tagFile syscall:

// The tagFile syscall must use the exact function signature that we have provided above.
// The tagFile syscall must tag the file specified by the file descriptor with the key-value pair that is passed in.
// Hint: you'll have to figure out where to store these tags.  One reasonable approach is to repurpose one of the inode's direct blocks for tag storage.  You can read more about inodes starting on page 77 of the xv6 textbook.
// If the file already has a tag with the specified key, then the specified value will overwrite the stored value.  In other words, if a file is tagged with "language": "English", you should be able to use tagFile to change the tag to "language": "Java".
// The tagFile syscall must validate the arguments passed in and return -1 as necessary to indicate an error.  Some cases to consider:
// The file descriptor must be opened in write mode in order to tag a file successfully.
// The key must be at least 2 bytes (including the null termination byte) and at most 10 bytes (including the null termination byte).
// You can restrict the disk space allotted to tags to 512 bytes per file.  In other words, you can require that all tag information for a given file must fit within a single 512-byte disk block.  If there isn't sufficient tag space for tagFile to complete, you can simply return -1.

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

int
tagFile(int fileDescriptor, char* key, char* value, int valueLength)
{
  struct file *f;
  struct buf *bp;
  uchar *str;
  int keyLength;
  if (fileDescriptor < 0 || fileDescriptor >= NOFILE || (f = proc->ofile[fileDescriptor]) == 0) return -1;
  if (f->type != FD_INODE || !f->writable || !f->ip) return -1;
  if (!key || (keyLength = strlen(key)) < 1 || keyLength > 9) return -1;
  if (!value || valueLength < 0 || valueLength > 18) return -1;
  ilock(f->ip);
  if (!f->ip->tags) f->ip->tags = balloc(f->ip->dev);
  bp = bread(f->ip->dev, f->ip->tags);
  str = (uchar*)bp->data;
  int keyPos = searchKey((uchar*)key, (uchar*)str);
  if (keyPos < 0) {
    int endPos = searchEnd((uchar*)str);
    if (endPos < 0) {
      brelse(bp);
      iunlock(f->ip);
      return -1;
    }
    memset((void*)((uint)str + (uint)endPos), 0, 28);
    memmove((void*)((uint)str + (uint)endPos), (void*)key, (uint)keyLength);
    memmove((void*)((uint)str + (uint)endPos + 10), (void*)value, (uint)valueLength);
    // for (int i = 0; i < BSIZE; i += 32) {
    //   cprintf("tagFile: key = %s\t value = %s\n", str + i, str + i + 10);

    // }
    // cprintf("\n");
    bwrite(bp);
    brelse(bp);
    iunlock(f->ip);
    return 1;
  }
  memset((void*)((uint)str + (uint)keyPos + 10), 0, 18);
  memmove((void*)((uint)str + (uint)keyPos + 10), (void*)value, (uint)valueLength);
  // cprintf("value = %s\n", value);
  bwrite(bp);
  brelse(bp);
  iunlock(f->ip);
  return 1;
}

// 2) removeFileTag

// Requirements (and hints)

// Here is a list of specific requirements related to the removeFileTag syscall:

// The removeFileTag syscall must use the exact function signature that we have provided above.
// The removeFileTag syscall must remove the specified tag from the specified file.
// The syscall should return -1 to indicate an error.  Here are some cases to consider:
// If the tag specified by key cannot be found or is invalid, return -1.
// If the file descriptor is not open and writable, return -1.
// The syscall should return 1 to indicate success.

int
removeFileTag(int fileDescriptor, char* key)
{
  struct file *f;
  int keyLength;
  struct buf *bp;
  uchar *str;
  if (fileDescriptor < 0 || fileDescriptor >= NOFILE || (f = proc->ofile[fileDescriptor]) == 0) return -1;
  if (f->type != FD_INODE || !f->writable || !f->ip || !f->ip->tags) return -1;
  if (!key || (keyLength = strlen(key)) < 1 || keyLength > 9) return -1;
  ilock(f->ip);
  bp = bread(f->ip->dev, f->ip->tags);
  str = (uchar*)bp->data;
  int keyPos = searchKey((uchar*)key, (uchar*)str);
  if (keyPos < 0) {
    brelse(bp);
    iunlock(f->ip);
    return -1;    
  }
  memset((void*)((uint)str + (uint)keyPos), 0, 28);
  bwrite(bp);
  brelse(bp);
  iunlock(f->ip);
  return 1;
}

// 3) getFileTag

// Requirements (and hints)

// Here is a list of specific requirements related to the getFileTag syscall:

// The getFileTag syscall must use the exact function signature that we have provided above.
// The syscall should return the length of the value part of the specified tag.  This returned length should NOT include any null-terminating byte.  In fact, since the length of the value is being passed around, you do not need to use a null-terminating byte at all.  If you do choose to use one, it should not be counted in the returned length.
// The value of the specified tag should be written to buffer.
// If the length of the value is longer than length, the syscall should return the actual length of the value.  This allows the user to decide whether to allocate a larger buffer and try again.
// The syscall should return -1 to indicate failure.  Here are some cases to consider:
// If the key cannot be found or is invalid, return -1.
// If the file descriptor is not open and readable, return -1.

int
getFileTag(int fileDescriptor, char* key, char* buffer, int length)
{
  struct file *f;
  int keyLength;
  // int valueLength;
  struct buf *bp;
  uchar str[BSIZE];
  uchar *value;
  if (fileDescriptor < 0 || fileDescriptor >= NOFILE || (f = proc->ofile[fileDescriptor]) == 0) return -1;
  if (f->type != FD_INODE || !f->readable || !f->ip) return -1;
  if (!key || (keyLength = strlen(key)) < 1 || keyLength > 9) return -1;
  if (!buffer) return -1;
  if (length < 0 || length > 18) return -1;
  ilock(f->ip);
  if (!f->ip->tags) f->ip->tags = balloc(f->ip->dev);
  bp = bread(f->ip->dev, f->ip->tags);
  memmove((void*)str, (void*)bp->data, (uint)BSIZE);
  brelse(bp);
  iunlock(f->ip);
  // for (int i = 0; i < BSIZE; i += 32) {
  //   cprintf("getFileTag: key = %s\t value = %s\n", str + i, str + i + 10);
  // }
  // cprintf("\n");
  int keyPos = searchKey((uchar*)key, (uchar*)str);
  // cprintf("getFileTag: keyPos = %s\tkey = %s\tvalue = %s\t", (char*)((uint)str + (uint)keyPos), key, (char*)((uint)str + (uint)keyPos + 10));
  if (keyPos < 0) return -1;
  value = (uchar*)((uint)str + (uint)keyPos + 10);
  // valueLength = 17;
  // while (valueLength >= 0 && !value[valueLength]) valueLength--;
  // valueLength++;
  // valueLength = strlen((char*)value);
  // if (!valueLength) return -1;
  memmove((void*)buffer, (void*)value, (uint)min(length, 18));
  // cprintf("buffer = %s\n", (char*)buffer);
  return min(length, 18);
}

int
getAllTags(int fileDescriptor, struct Key *keys, int maxTags)
{
  struct file *f;
  struct buf *bp;
  uchar str[BSIZE];
  uint i = 0;
  int j = 0;
  if (fileDescriptor < 0 || fileDescriptor >= NOFILE || (f = proc->ofile[fileDescriptor]) == 0) return -1;
  if (f->type != FD_INODE || !f->readable || !f->ip) return -1;
  if (!keys) return -1;
  if (maxTags < 0) return -1;
  ilock(f->ip);
  if (!f->ip->tags) f->ip->tags = balloc(f->ip->dev);
  bp = bread(f->ip->dev, f->ip->tags);
  memmove((void*)str, (void*)bp, (uint)BSIZE);
  brelse(bp);
  iunlock(f->ip);
  for (i = 0, j = 0; i < BSIZE; i += 32) {
    if (str[i]) {
      memmove((void*)keys[j].key, (void*)((uint)str + i), (uint)strlen((char*)((uint)str + (uint)i)));
      j++;
    }
  }
  return j;
}