#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"


// Wrapper Function of System call
int sys_setPriority(void) {
  int a, b;
  if(argint(0, &a) < 0) {
    return -1;
  }
  if(argint(0, &b) < 0) {
    return -2;
  }
  setPriority(a, b);
  return 0;
}

int sys_getLevel(void) {
    return getLevel();
}

int sys_yield(void) {
    yield();
    return 0;
}

int sys_schedulerLock(void) {
    int a;
    if(argint(0, &a) < 0) {
        return -1;
    }
    schedulerLock(a);
    return 0;
}

int sys_schedulerUnlock(void) {
    int a;
    if(argint(0, &a) < 0) {
        return -1;
    }
    schedulerUnlock(a);
    return 0;
}
  
