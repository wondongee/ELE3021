#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include "elf.h"




struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
int nexttid = 1;
extern void forkret(void);
extern void trapret(void);
extern int mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm);

static void wakeup1(void *chan);

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
  p->limit = 0;
  p->stackPageNum = 0;
  p->tid = 0;
  p->mainpid = 0;
  p->tparent = 0;
  release(&ptable.lock);

  // Allocate kernel stack.
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

  // 추가적인 메모리를 할당 받을 때 프로세스의 memory limit을 넘지 않도록 함.
  if (curproc->limit != 0 && curproc->limit < sz + n) {
    cprintf("[memory limit 초과] 추가적인 메모리 할당이 불가능합니다.");
    return -1;
  }
  acquire(&ptable.lock);

  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }

  release(&ptable.lock);
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

  // thread에서 호출되었을 때 thread 추가
  if(curproc->tid != 0) {
    np->tid = nexttid++;
    np->parent = curproc->parent;
    np->mainpid = curproc->mainpid;
    np->tparent = curproc;
  } else {
    np->parent = curproc;
  }
  
  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  *np->tf = *curproc->tf;
  np->stackPageNum = curproc->stackPageNum;
  np->limit = curproc->limit;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));
  pid = np->pid;

  acquire(&ptable.lock);
  //cprintf("Fork[parent]: pid %d\t tid %d\t sz = %d\n", np->parent->pid, np->parent->tid, np->parent->sz);
  //cprintf("Fork[child] : pid %d\t tid %d\t sz = %d\n", np->pid, np->tid, np->sz);
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
  cprintf("exit tid %d\n", curproc->tid);
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
  // thread이면
  
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE) {
        cprintf("initproc exit\n");
        wakeup1(initproc);
      }
    }
  }

  // Jump into the scheduler, never to return.
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
      if(p->parent == curproc || p->tparent == curproc) {
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
          p->stackPageNum = 0;
          p->limit = 0;
          p->mainpid = 0;
          p->tid = 0;
          p->retval = 0;
          p->state = UNUSED;
          release(&ptable.lock);
          return pid;
        }
      }
    }
    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }
    cprintf("sleep Mode");
    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
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
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
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
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
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
  struct proc *pt;
  struct proc *curproc = myproc();
  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid) {
      curproc = p;
    }
  }
  
  // thread가 kill 되었을 때 같은 thread도 kill
  if (curproc->tid > 0) {
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if (p->tid > 0) {
        if(p->mainpid == curproc->mainpid) {
          p->killed = 1;
          if (p->state == SLEEPING)
            p->state = RUNNABLE;
        }
      }
    }
    release(&ptable.lock);
    return 0;
  } else {  // main process가 kill 되었을 때           
    for(pt = ptable.proc; pt < &ptable.proc[NPROC]; pt++){
      if( (pt->tid > 0) && (pt->mainpid == curproc->pid) ) {
        cprintf("hello\n");
        pt->killed = 1;
        if(pt->state == SLEEPING)
          pt->state = RUNNABLE;
      }
    }
    cprintf("hello\n");
    curproc->killed = 1;
    if(curproc->state == SLEEPING)
      curproc->state = RUNNABLE;
    release(&ptable.lock);
    return 0;    
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

int
exec2(char *path, char **argv, int stacksize)
{
  cprintf("exec2 함수가 실행되었습니다.\n");
  char *s, *last;
  int i, off;
  uint argc, sz, sp, ustack[3+MAXARG+1];
  struct elfhdr elf;
  struct inode *ip;
  struct proghdr ph;
  pde_t *pgdir, *oldpgdir;
  struct proc *curproc = myproc();

  begin_op();

  if((ip = namei(path)) == 0){
    end_op();
    cprintf("exec: fail\n");
    return -1;
  }
  ilock(ip);
  pgdir = 0;

  if(readi(ip, (char*)&elf, 0, sizeof(elf)) != sizeof(elf))
    goto bad;
  if(elf.magic != ELF_MAGIC)
    goto bad;

  if((pgdir = setupkvm()) == 0)
    goto bad;

  sz = 0;
  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
    if(readi(ip, (char*)&ph, off, sizeof(ph)) != sizeof(ph))
      goto bad;
    if(ph.type != ELF_PROG_LOAD)
      continue;
    if(ph.memsz < ph.filesz)
      goto bad;
    if(ph.vaddr + ph.memsz < ph.vaddr)
      goto bad;
    if((sz = allocuvm(pgdir, sz, ph.vaddr + ph.memsz)) == 0)
      goto bad;
    if(ph.vaddr % PGSIZE != 0)
      goto bad;
    if(loaduvm(pgdir, (char*)ph.vaddr, ip, ph.off, ph.filesz) < 0)
      goto bad;
  }
  iunlockput(ip);
  end_op();
  ip = 0;

  // 스택용 페이지를 여러 개 할당받을 수 있도록 변경
  // Make the first inaccessible.  Use the second as the user stack.
  if (stacksize > 100 || stacksize < 1)
    goto bad;
  sz = PGROUNDUP(sz);

  // stack용 가상주소공간 할당 old size -> new size
  if((sz = allocuvm(pgdir, sz, sz + (stacksize+1)*PGSIZE)) == 0)
    goto bad;
  clearpteu(pgdir, (char*)(sz - (stacksize+1)*PGSIZE));
  sp = sz;

  // Push argument strings, prepare rest of stack in ustack.
  for(argc = 0; argv[argc]; argc++) {
    if(argc >= MAXARG)
      goto bad;
    sp = (sp - (strlen(argv[argc]) + 1)) & ~3;
    if(copyout(pgdir, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
      goto bad;
    ustack[3+argc] = sp;
  }
  ustack[3+argc] = 0;

  ustack[0] = 0xffffffff;  // fake return PC
  ustack[1] = argc;
  ustack[2] = sp - (argc+1)*4;  // argv pointer

  sp -= (3+argc+1) * 4;
  if(copyout(pgdir, sp, ustack, (3+argc+1)*4) < 0)
    goto bad;

  // Save program name for debugging.
  for(last=s=path; *s; s++)
    if(*s == '/')
      last = s+1;
  safestrcpy(curproc->name, last, sizeof(curproc->name));

  // Commit to the user image.
  oldpgdir = curproc->pgdir;
  curproc->pgdir = pgdir;
  curproc->sz = sz;
  curproc->tf->eip = elf.entry;  // main
  curproc->tf->esp = sp;
  // stackPageNum 값 변경
  curproc->stackPageNum = stacksize + 1 + curproc->stackPageNum;
  switchuvm(curproc);
  freevm(oldpgdir);
  return 0;

 bad:
  if(pgdir)
    freevm(pgdir);
  if(ip){
    iunlockput(ip);
    end_op();
  }
  return -1;
}

int setmemorylimit(int pid, int limit) {
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      // limit이 0인 경우 제한이 없음
      if (p->limit == 0 && limit == 0) {
        release(&ptable.lock);
        return 0;      
      } else if (p->limit != 0 && limit == 0) {
        p->limit = 0;
        release(&ptable.lock);
        return 0;
      // limit이 음수인 경우
      } else if (limit < 0) {
        release(&ptable.lock);
        return -1;
      // 기존 할당받은 메모리보다 limit이 작은 경우
      } else if (limit < p->sz) {
        release(&ptable.lock);
        return -1;
      }
      // limit이 양수인 경우
      p->limit = limit;
      release(&ptable.lock);
      return 0;
    }
  }
  // pid가 존재하지 않는 경우
  release(&ptable.lock);
  return -1;
}


void list_process(void) {
  struct proc *p;
  acquire(&ptable.lock);
    
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if (p->state == RUNNABLE || p->state == RUNNING || p->state == SLEEPING || p->state == ZOMBIE) {    
      // procstate { UNUSED, EMBRYO, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };
      //if(p->tid == 0) {
        cprintf("pid %d\t %s\t %d\t stackPageNum %d\t size %d\t memoryLimit %d tid %d\n",
            p->pid, p->name, p->state, p->stackPageNum, p->sz, p->limit, p->tid);
      //}      
    }
  }
  release(&ptable.lock);
  return;
}

// generate new thread 
int thread_create(thread_t *thread, void* (*start_routine)(void*), void* arg) {
    
    int i;
    struct proc *np;
    struct proc *curproc = myproc();
    uint ustack[2];

    // allocate new thread
    if((np = allocproc()) == 0) {
        return -1;
    }
    

    // main thread의 pid 공유
    if(curproc->tid == 0) { // curproc이 메인 쓰레드
      np->mainpid = curproc->pid;
      np->parent = curproc;
      np->tparent = curproc;
    } else if (curproc->tid > 0) { // curproc이 일반 쓰레드
      np->mainpid = curproc->mainpid;
      np->parent = curproc->parent;
      np->tparent = curproc;
    }        
    
    // 프로세스 페이지 테이블 공유
    np->pgdir = np->parent->pgdir;
    np->tid = nexttid++;
    *thread = np->tid;
    *np->tf = *curproc->tf;
    np->sz = np->parent->sz;
    np->parent->sz += 2*PGSIZE;
    
    // 쓰레드 스택 할당
    if((np->sz = allocuvm(np->pgdir, np->sz, np->sz + 2*PGSIZE)) == 0) {
        np->state = UNUSED;        
        return -1;
    }

    

    safestrcpy(np->name, curproc->name, sizeof(curproc->name));
    np->cwd = idup(curproc->cwd);
    for(i=0; i<NOFILE; i++)
        if(curproc->ofile[i])
            np->ofile[i] = filedup(curproc->ofile[i]);

    clearpteu(np->pgdir, (char*)(np->sz - 2*PGSIZE));
    
    // 쓰레드는 독립적인 1개의 가드 페이지 + 1개의 스택 페이지 할당됨
    np->stackPageNum = 2; 
    
    ustack[0] = 0xffffffff;
    ustack[1] = (uint)arg;
    np->tf->esp = np->sz - 8;
    if(copyout(np->pgdir, np->tf->esp, ustack, 8) != 0) {
      return -1;
    }    
    np->tf->eip = (uint)start_routine; 
    // acquire ptable lock
    acquire(&ptable.lock);
    cprintf("Thread[parent]: pid %d\t tid %d\t sz = %d\t mainpid %d\n", np->parent->pid, np->parent->tid, np->parent->sz, np->parent->mainpid);
    cprintf("Thread[child] : pid %d\t tid %d\t sz = %d\t mainpid %d\n", np->pid, np->tid, np->sz, np->mainpid);
    np->state = RUNNABLE;    
    release(&ptable.lock);    

    return 0;
}

// function to set the LWP's status as ZOMBIE
void thread_exit(void* retval) {
    struct proc *p;    
    struct proc *curproc = myproc();
    int fd;
    cprintf("thread exit call\n");

    // initproc should not exit
    if(curproc == initproc)
        panic("init exiting\n");

    // Close all open files.
    for(fd=0; fd<NOFILE; fd++) {
        if(curproc->ofile[fd]) {
            fileclose(curproc->ofile[fd]);
            curproc->ofile[fd] = 0;
        }
    }

    begin_op();
    iput(curproc->cwd);
    end_op();
    curproc->cwd = 0;
    // save return value
    curproc->retval = retval;

    acquire(&ptable.lock);   
    wakeup1(curproc->parent);
    
    // Pass abandoned children to init.
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent == curproc){
        p->parent = initproc;
        if(p->state == ZOMBIE) {
          //cprintf("initproc exit h\n");
          wakeup1(initproc);
        }
      }
    }

    // Jump into the scheduler, never to return.
    curproc->state = ZOMBIE; 
    sched();    
    panic("zombie exit\n");
}

// 쓰레드가 종료될 때까지 기다림
int thread_join(thread_t thread, void** retval) {    
    cprintf("thread join call\n");

    struct proc *p;
    struct proc *curproc = myproc();
    int flg;

    acquire(&ptable.lock);
    for(;;) {
        flg = 0;
        for(p=ptable.proc; p<&ptable.proc[NPROC]; p++) {                  
            if(p->tid != thread)
                continue;
            flg = 1;          
            if(p->state == ZOMBIE) {       
                *retval = p->retval;

                // free up basic resources
                kfree(p->kstack);
                p->kstack = 0;
                //deallocuvm(p->pgdir, p->sz, p->sz - (p->stackPageNum)*PGSIZE);                                                              
                p->pid = 0;                
                p->parent = 0;
                p->name[0] = 0;
                p->killed = 0;                
                p->tid = 0;
                p->mainpid = 0;
                p->stackPageNum = 0;
                p->limit = 0;
                p->retval = 0;
                p->state = UNUSED;
                release(&ptable.lock);                
                return 0;
            }
        }
        if(!flg) {
          release(&ptable.lock);
          return -1;
        }
        cprintf("thread sleep mode.\n");
        sleep(curproc, &ptable.lock);
    }              
}

void clear_thread(void) {
  struct proc *curproc = myproc();
  struct proc *p;  
  acquire(&ptable.lock);
  // main 스레드이면 기존의 스레드 모두 정리
  if(curproc->tid == 0) {
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if (p->parent == curproc && p->tid > 0) {
        p->killed = 1;
        if(p->state == SLEEPING) {
          p->state = RUNNABLE;
        }
      }
    }
  } else {
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if (p->mainpid == curproc->mainpid && p != curproc) {
        p->killed = 1;
        if(p->state == SLEEPING) {
          p->state = RUNNABLE;
        }
      }
    }    
  }
  release(&ptable.lock);
  return;
}



