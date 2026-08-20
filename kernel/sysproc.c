#include "types.h"
#include "riscv.h"
#include "param.h"
#include "defs.h"
#include "date.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;


  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}


#ifdef LAB_PGTBL
int
sys_pgaccess(void)
{
 uint64 va;
  int n;
  uint64 user_buf_addr;
  unsigned int bitmask = 0;

  // 获取系统调用3个参数
  if(argaddr(0, &va) < 0)
    return -1;
  if(argint(1, &n) < 0)
    return -1;
  if(argaddr(2, &user_buf_addr) < 0)
    return -1;

  // 参数合法性校验，不能超过最大虚拟地址
  if(va >= MAXVA || n < 0)
    return -1;
  if(va + n * PGSIZE >= MAXVA)
    return -1;

  struct proc *p = myproc();
  pagetable_t pt = p->pagetable;

  for(int i = 0; i < n; i++){
    uint64 curr_va = va + i * PGSIZE;
    pagetable_t pagetable = pt;
    pte_t *pte;

    // 复刻walk，逐级找页表项，不分配页表
    for(int level = 2; level > 0; level--){
      pte = &pagetable[PX(level, curr_va)];
      if(!(*pte & PTE_V)){
        return -1;
      }
      pagetable = (pagetable_t) PTE2PA(*pte);
    }
    pte = &pagetable[PX(0, curr_va)];
    if(!(*pte & PTE_V)){
      return -1;
    }

    // 判断访问位PTE_A
    if(*pte & PTE_A){
      bitmask |= (1U << i);
      // 清除访问位，关键步骤
      *pte &= ~PTE_A;
    }
  }

  // 将bitmask复制回用户空间
  if(copyout(p->pagetable, user_buf_addr, (char *)&bitmask, sizeof(unsigned int)) < 0){
    return -1;
  }
  return 0;
}
#endif

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}
