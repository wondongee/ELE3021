#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

extern uint global_ticks;        // trap.c 파일의 global_ticks 변수를 가져옴
int lock_ticks;                  // schedulerLock()이 진행되고 경과된 시간을 저장함

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

void
pinit(void)
{
  initlock(&ptable.lock, "ptable"); 
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  // 프로세스 멤버 초기화
  p->priority = 3;
  p->used_time = 0;
  p->level = 0;
  p->locked = 0;
  p->arrival_time = global_ticks;

  release(&ptable.lock);

  // Allocate kernel.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  initproc = p;

  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S  

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.

  acquire(&ptable.lock);
  p->state = RUNNABLE;
  release(&ptable.lock);

}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;
  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);
  safestrcpy(np->name, curproc->name, sizeof(curproc->name));
  pid = np->pid;
  acquire(&ptable.lock);
  np->state = RUNNABLE;
  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }

}


void
scheduler(void)
{
  struct proc *p;
  
  struct cpu *c = mycpu();
  c->proc = 0;

  for(;;){
    
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    // arrival_time이 가장 작은 먼저 도착한 프로세스 저장
    struct proc *fcfs = 0;
    // runnable한 process 찾았으면 1로 변경
    uint flag = 0;          
    // arrival_time이 가장 작은 프로세스의 도착시간 저장 (FCFS 정책을 위해)
    uint min_time = 999999; 

    // level 0 -> level 1 -> level 2 순서로 runnable한 프로세스 탐색
    for(int i = 0; i < NMLFQ; i++) {
      // level 0과 1은 Round Robin + FCFS 스케줄링
      if(i == 0 || i == 1) {        
        for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){ 
          // ptable 탐색하면서 RUNNABLE 이고 level=i 인 프로세스 찾음
          if(p->state != RUNNABLE || p->level != i) {
            continue;
          }
          // FCFS 정책을 위해 같은 level에서는 arrival_time이 제일 작은 프로세스 찾음
          if ( p->arrival_time <= min_time ) {
            min_time = p->arrival_time;
            fcfs = p; // Runnable한 프로세스를 fcfs 포인터 변수에 저장
            flag = 1; // Runnable한 프로세스 찾았으면 flag = 1 로 변경
          }        
        }
        // level i의 arrival_time이 최소인 runnable한 프로세스 찾았으면 execute로 가서 실행
        if (flag == 1) {
          p = fcfs;
          goto execute;
        }        
      } else if (i==2) {     // level 2는 Priority + FCFS 스케줄링
        // L2 큐에서는 priority가 낮은 순서대로 탐색함 0 -> 1 -> 2 -> 3
        for (int j = 0; j <= 3; j++) {
          for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){ 
            // ptable 탐색하면서 RUNNABLE 이고 level=2 이고 특정한 priority를 가지는 프로세스만 찾음
            if (p->state != RUNNABLE || p->level != i || p->priority != j) {
              continue;
            }
            // FCFS 정책을 위해 같은 level, 같은 priority에서는 arrival_time이 제일 작은 프로세스 찾음
            if ( p->arrival_time <= min_time ) {
              min_time = p->arrival_time;
              fcfs = p;
              flag = 1; 
            }                      
          }
          // level 2의 특정한 priority를 가지는 runnable한 프로세스 찾았으면 execute로 가서 실행
          if (flag == 1) {
            p = fcfs;
            goto execute;
          }
        }            
      }
    }
    // runnable한 프로세스를 찾지 못했으면 첫 for문으로 돌아가서 재탐색
    release(&ptable.lock);
    continue;

    execute:
      c->proc = p;              
      switchuvm(p);
      p->state = RUNNING;
      swtch(&(c->scheduler), p->context);
      switchkvm();
      c->proc = 0;
      release(&ptable.lock);
  }
}


// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.


void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  // schedulerLocked
  if(p->locked == 1) {
    p->level = 0;
    p->arrival_time = 0;
    p->priority = 3;
    p->used_time = 0;    
    // global ticks가 0
    if (global_ticks >= 100) {
      p->locked = 0;
    }
  } 
  // global_ticks가 100보다 커지면 priority boost 발생
  if(global_ticks >= 100) {
    priority_boost();
  }
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  
  int level = myproc()->level;

  // level 0에 있는 프로세스에서 yield 발생
  if(level == 0 && myproc()->used_time >= 2*level + 4) {
      myproc()->used_time = 0;                // 각 레벨에서 사용한 time quantum 초기화
      myproc()->level++;                      // 프로세스를 다음 레벨로 증가시킴
      myproc()->arrival_time = global_ticks;  // 다음 레벨의 arrival_time을 global_ticks로 초기화
  
  // level 1에 있는 프로세스에서 yield 발생
  } else if (level == 1 && myproc()->used_time >= 2*level + 4) {
      myproc()->used_time = 0;
      myproc()->level++;
      myproc()->arrival_time = global_ticks;
  
  // level 2에 있는 프로세스에서 yield 발생
  } else if(level == 2 && myproc()->used_time >= 2*level + 4) {
      myproc()->used_time = 0;
      if (myproc()->priority != 0) {          // priority를 감소시킴 (이미 0이면 감소 x)
        myproc()->priority--;
      }      
  }  
  sched();
  release(&ptable.lock);
}
      //cprintf("level 0의 프로세스에서 timequnatum yield 발생 : %d used_time\n", myproc()->used_time);
      //cprintf("level 1의 프로세스에서 timequnatum yield 발생 : %d used_time\n", myproc()->used_time);
      //cprintf("level 2의 프로세스에서 timequnatum yield 발생 : %d used_time\n", myproc()->used_time);




void
forkret(void)
{
  static int first = 1;
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

void priority_boost(void) {  
  struct proc* p;
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) { 
    p->priority = 3;
    p->level = 0;
    p->used_time = 0;
    global_ticks = 0;
  }
}

// 해당 pid의 프로세스의 priority를 설정합니다.
void setPriority(int pid, int priority) {
  struct proc* p;

  // 설정되는 priority가 0보다 작거나 3보다 클 경우 리턴
  if(priority<0 || priority>3) {
    return;
  }
  
  // ptable을 탐색하며 입력 pid와 일치하는 프로세스를 찾아서 priority 재조정
  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) { 
    if(p->pid == pid) {
      p->priority = priority;
    }
  }
  release(&ptable.lock);
  return;
}

// 프로세스가 속한 큐의 레벨을 반환합니다.
int getLevel(void) {
  struct proc *curproc = myproc();

  return curproc->level;
}

// 해당 프로세스가 우선적으로 스케줄링 되도록 합니다.
void schedulerLock(int password) {
  int num = 2017033654;
  
  // 입력으로 받은 password와 학번이 일치하면 Lock을 실행
  if(password == num) {
    acquire(&ptable.lock);
    global_ticks = 0;
    // 현재 프로세스를 lock 상태로 변환
    myproc()->locked = 1;
    // level 0의 제일 높은 우선순위를 부여해서 제일 먼저 스케줄링 되도록 함
    myproc()->level = 0;
    myproc()->arrival_time = 0;
    release(&ptable.lock);
  } else {
    // 암호가 일치하지 않으면 프로세스 강제 종료
    cprintf("pid = %d, time quantum = %d, level = %d", 
      myproc()->pid, myproc()->used_time, myproc()->level);
    kill(myproc()->pid);    
  }  
}

// 해당 프로세스가 우선적으로 스케줄링 되던 것을 중지합니다.
void schedulerUnlock(int password) {
  int num = 2017033654;

  if(password == num) {
    acquire(&ptable.lock);
    // 암호가 일치하면 현재 프로세스를 unlock 상태로 변환
    myproc()->locked = 0;
    myproc()->level = 0;
    myproc()->arrival_time = 0;
    myproc()->priority = 3;
    myproc()->used_time = 0;
    release(&ptable.lock);
  } else {
    // 암호가 일치하지 않으면 프로세스 강제 종료
    cprintf("pid = %d, time quantum = %d, level = %d", 
      myproc()->pid, myproc()->used_time, myproc()->level);
    kill(myproc()->pid);    
  }
}