//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  if(argint(n, &fd) < 0)
    return -1;
  if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == 0)
    return -1;
  if(pfd)
    *pfd = fd;
  if(pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *p = myproc();

  for(fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd] == 0){
      p->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

uint64
sys_dup(void)
{
  struct file *f;
  int fd;

  if(argfd(0, 0, &f) < 0)
    return -1;
  if((fd=fdalloc(f)) < 0)
    return -1;
  filedup(f);
  return fd;
}

uint64
sys_read(void)
{
  struct file *f;
  int n;
  uint64 p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;
  return fileread(f, p, n);
}

uint64
sys_write(void)
{
  struct file *f;
  int n;
  uint64 p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;

  return filewrite(f, p, n);
}

uint64
sys_close(void)
{
  int fd;
  struct file *f;

  if(argfd(0, &fd, &f) < 0)
    return -1;
  myproc()->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

uint64
sys_fstat(void)
{
  struct file *f;
  uint64 st; // user pointer to struct stat

  if(argfd(0, 0, &f) < 0 || argaddr(1, &st) < 0)
    return -1;
  return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
uint64
sys_link(void)
{
  char name[DIRSIZ], new[MAXPATH], old[MAXPATH];
  struct inode *dp, *ip;

  if(argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
    return -1;

  begin_op();
  if((ip = namei(old)) == 0){
    end_op();
    return -1;
  }

  ilock(ip);
  if(ip->type == T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }

  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  if((dp = nameiparent(new, name)) == 0)
    goto bad;
  ilock(dp);
  if(dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0){
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip);

  end_op();

  return 0;

bad:
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  end_op();
  return -1;
}

// Is the directory dp empty except for "." and ".." ?
static int
isdirempty(struct inode *dp)
{
  int off;
  struct dirent de;

  for(off=2*sizeof(de); off<dp->size; off+=sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if(de.inum != 0)
      return 0;
  }
  return 1;
}

uint64
sys_unlink(void)
{
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ], path[MAXPATH];
  uint off;

  if(argstr(0, path, MAXPATH) < 0)
    return -1;

  begin_op();
  if((dp = nameiparent(path, name)) == 0){
    end_op();
    return -1;
  }

  ilock(dp);

  // Cannot unlink "." or "..".
  if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  if((ip = dirlookup(dp, name, &off)) == 0)
    goto bad;
  ilock(ip);

  if(ip->nlink < 1)
    panic("unlink: nlink < 1");
  if(ip->type == T_DIR && !isdirempty(ip)){
    iunlockput(ip);
    goto bad;
  }

  memset(&de, 0, sizeof(de));
  if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  if(ip->type == T_DIR){
    dp->nlink--;
    iupdate(dp);
  }
  iunlockput(dp);

  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);

  end_op();

  return 0;

bad:
  iunlockput(dp);
  end_op();
  return -1;
}

static struct inode*
create(char *path, short type, short major, short minor)
{
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if((dp = nameiparent(path, name)) == 0)
    return 0;

  ilock(dp);

  if((ip = dirlookup(dp, name, 0)) != 0){
    iunlockput(dp);
    ilock(ip);
    if(type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE))
      return ip;
    iunlockput(ip);
    return 0;
  }

  if((ip = ialloc(dp->dev, type)) == 0)
    panic("create: ialloc");

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if(type == T_DIR){  // Create . and .. entries.
    dp->nlink++;  // for ".."
    iupdate(dp);
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      panic("create dots");
  }

  if(dirlink(dp, name, ip->inum) < 0)
    panic("create: dirlink");

  iunlockput(dp);

  return ip;
}

uint64
sys_open(void)
{
  char path[MAXPATH];
  int fd, omode;
  struct file *f;
  struct inode *ip;
  int n;

  if((n = argstr(0, path, MAXPATH)) < 0 || argint(1, &omode) < 0)
    return -1;

  begin_op();

  if(omode & O_CREATE){
    ip = create(path, T_FILE, 0, 0);
    if(ip == 0){
      end_op();
      return -1;
    }
  } else {
    if((ip = namei(path)) == 0){
      end_op();
      return -1;
    }
    ilock(ip);
    if(ip->type == T_DIR && omode != O_RDONLY){
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  if(ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)){
    iunlockput(ip);
    end_op();
    return -1;
  }

  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }

  if(ip->type == T_DEVICE){
    f->type = FD_DEVICE;
    f->major = ip->major;
  } else {
    f->type = FD_INODE;
    f->off = 0;
  }
  f->ip = ip;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  if((omode & O_TRUNC) && ip->type == T_FILE){
    itrunc(ip);
  }

  iunlock(ip);
  end_op();

  return fd;
}

uint64
sys_mkdir(void)
{
  char path[MAXPATH];
  struct inode *ip;

  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_mknod(void)
{
  struct inode *ip;
  char path[MAXPATH];
  int major, minor;

  begin_op();
  if((argstr(0, path, MAXPATH)) < 0 ||
     argint(1, &major) < 0 ||
     argint(2, &minor) < 0 ||
     (ip = create(path, T_DEVICE, major, minor)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_chdir(void)
{
  char path[MAXPATH];
  struct inode *ip;
  struct proc *p = myproc();
  
  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);
  if(ip->type != T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  iput(p->cwd);
  end_op();
  p->cwd = ip;
  return 0;
}

uint64
sys_exec(void)
{
  char path[MAXPATH], *argv[MAXARG];
  int i;
  uint64 uargv, uarg;

  if(argstr(0, path, MAXPATH) < 0 || argaddr(1, &uargv) < 0){
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv)){
      goto bad;
    }
    if(fetchaddr(uargv+sizeof(uint64)*i, (uint64*)&uarg) < 0){
      goto bad;
    }
    if(uarg == 0){
      argv[i] = 0;
      break;
    }
    argv[i] = kalloc();
    if(argv[i] == 0)
      goto bad;
    if(fetchstr(uarg, argv[i], PGSIZE) < 0)
      goto bad;
  }

  int ret = exec(path, argv);

  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);

  return ret;

 bad:
  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);
  return -1;
}

uint64
sys_pipe(void)
{
  uint64 fdarray; // user pointer to array of two integers
  struct file *rf, *wf;
  int fd0, fd1;
  struct proc *p = myproc();

  if(argaddr(0, &fdarray) < 0)
    return -1;
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    if(fd0 >= 0)
      p->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  if(copyout(p->pagetable, fdarray, (char*)&fd0, sizeof(fd0)) < 0 ||
     copyout(p->pagetable, fdarray+sizeof(fd0), (char *)&fd1, sizeof(fd1)) < 0){
    p->ofile[fd0] = 0;
    p->ofile[fd1] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  return 0;
}

// mmap(addr, length, prot, flags, fd, offset): map the file referenced by
// fd into the process's address space.  The kernel chooses the virtual
// address (addr is assumed to be zero).  Returns the address on success,
// or -1 (0xffffffffffffffff) on failure.
uint64
sys_mmap(void)
{
  uint64 va;
  int len, prot, flags, fd, off;
  struct proc *p = myproc();

  if(argaddr(0, &va) < 0)
    return -1;
  if(argint(1, &len) < 0 || argint(2, &prot) < 0 ||
     argint(3, &flags) < 0 || argint(4, &fd) < 0 || argint(5, &off) < 0)
    return -1;

  if(len <= 0)
    return -1;
  if(fd < 0 || fd >= NOFILE || p->ofile[fd] == 0)
    return -1;

  // A read-only file cannot be mapped writable with MAP_SHARED.
  if(p->ofile[fd]->writable == 0 &&
     (prot & PROT_WRITE) &&
     (flags & MAP_SHARED))
    return -1;

  // Find a free VMA entry.
  int i;
  for(i = 0; i < MAXVMA; i++){
    if(p->vma[i].valid == 0){
      p->vma[i].valid = 1;
      break;
    }
  }
  if(i == MAXVMA)
    return -1;

  // Allocate a virtual address range from the top of the heap area.
  p->curend = PGROUNDDOWN(p->curend - len);
  p->vma[i].va = p->curend;
  p->vma[i].len = len;

  // Translate prot into PTE flags.
  uint pteflags = 0;
  if(prot & PROT_READ)
    pteflags |= PTE_R;
  if(prot & PROT_WRITE)
    pteflags |= PTE_W;
  p->vma[i].prot = pteflags;
  p->vma[i].flags = flags;
  p->vma[i].fd = fd;
  p->vma[i].off = off;
  p->vma[i].f = p->ofile[fd];

  // Keep the file alive even after the fd is closed.
  filedup(p->vma[i].f);

  return p->vma[i].va;
}

// munmap(addr, length): unmap the pages in the given range.
uint64
sys_munmap(void)
{
  uint64 va;
  int len;

  if(argaddr(0, &va) < 0)
    return -1;
  if(argint(1, &len) < 0)
    return -1;

  // The heavy lifting is shared with exit().
  if(subunmap(va, len) == -1)
    return -1;
  return 0;
}

// Handle a page fault at (page-aligned) user virtual address va.
// If va falls inside one of the process's mmap-ed regions, allocate a
// physical page, read the relevant 4096 bytes of the file into it, and
// map it into the user address space.  Returns 0 on success, -1 otherwise.
uint64
pgfault(uint64 va)
{
  struct proc *p = myproc();
  struct vma_t *v = 0;

  for(int i = 0; i < MAXVMA; i++){
    if(p->vma[i].valid && va >= p->vma[i].va &&
       va < p->vma[i].va + p->vma[i].len){
      v = &p->vma[i];
      break;
    }
  }
  if(v == 0)
    return -1;

  // The faulting address must be page-aligned already.
  uint64 pa = (uint64)kalloc();
  if(pa == 0)
    return -1;
  memset((char *)pa, 0, PGSIZE);

  // Map the page first so readi() can copy data into it.
  if(mappages(p->pagetable, va, PGSIZE, pa, v->prot | PTE_U) != 0){
    kfree((void *)pa);
    return -1;
  }

  // Read up to 4096 bytes of the file into the page.
  ilock(v->f->ip);
  int ret = readi(v->f->ip, 1, va, v->off + (va - v->va), PGSIZE);
  iunlock(v->f->ip);
  if(ret < 0){
    uvmunmap(p->pagetable, va, 1, 1);
    return -1;
  }

  return 0;
}

// Find the VMA containing va.
static struct vma_t*
findvma(struct proc *p, uint64 va)
{
  for(int i = 0; i < MAXVMA; i++){
    if(p->vma[i].valid && va >= p->vma[i].va && va < p->vma[i].va + p->vma[i].len)
      return &p->vma[i];
  }
  return 0;
}

// Unmap len bytes starting at (possibly non-page-aligned) address va.
// This is shared by sys_munmap and exit.  Writes back MAP_SHARED dirty
// pages, adjusts or frees the VMA, and drops the file reference when the
// whole region is unmapped.  Returns 0 on success, -1 on failure.
uint64
subunmap(uint64 va, int len)
{
  if(len <= 0)
    return 0;

  struct proc *p = myproc();
  struct vma_t *v = findvma(p, va);
  if(v == 0)
    return -1;

  // Page-align the unmap range.  The lab guarantees the range starts at
  // the beginning or the end of the region, or covers the whole region.
  uint64 start = PGROUNDUP(va);
  uint64 end = PGROUNDDOWN(va + len);

  // Write back and unmap each mapped page.
  for(uint64 a = start; a < end; a += PGSIZE){
    pte_t *pte = walk(p->pagetable, a, 0);
    if(pte == 0 || (*pte & PTE_V) == 0)
      continue;
    if(v->flags & MAP_SHARED){
      // The dirty bit may not be set in this emulation; write back anyway.
      begin_op();
      ilock(v->f->ip);
      if(writei(v->f->ip, 1, a, v->off + (a - v->va), PGSIZE) < 0){
        iunlock(v->f->ip);
        end_op();
        return -1;
      }
      iunlock(v->f->ip);
      end_op();
    }
    uvmunmap(p->pagetable, a, 1, 1);
  }

  // Adjust the VMA: four cases depending on whether the unmap starts at
  // the region's start and whether it spans the region's full length.
  uint64 vma_end = v->va + v->len;
  if(va == v->va && (uint64)len == v->len){
    // Whole region.
    v->len = 0;
  } else if(va == v->va){
    // Unmapping from the start: move the start up.
    v->len -= len;
    v->va += len;
    v->off += len;
  } else if(va + len == vma_end){
    // Unmapping from the end: shrink the length.
    v->len -= len;
  } else {
    // Punching a hole in the middle is not supported by the lab.
    return -1;
  }

  // If the whole region is gone, drop the file reference.
  if(v->len == 0){
    fileclose(v->f);
    v->f = 0;
    v->va = 0;
    v->fd = 0;
    v->off = 0;
    v->valid = 0;

    // Recompute curend as the lowest address among the remaining VMAs,
    // or the top of the heap area if none remain.
    uint64 top = MAXVA - 2 * PGSIZE;
    uint64 lowest = top;
    for(int i = 0; i < MAXVMA; i++){
      if(p->vma[i].valid && p->vma[i].va < lowest)
        lowest = p->vma[i].va;
    }
    p->curend = lowest;
  }

  return 0;
}
