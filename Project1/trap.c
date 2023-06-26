#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"

// Interrupt descriptor table (shared by all CPUs).
// idt는 gatedesc로 이루어져 있음
struct gatedesc idt[256];
extern uint vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;
uint global_ticks = 0;


void
tvinit(void)
{
  int i;

  for(i = 0; i < 256; i++)
    // interrupt 초기화 system call제외 나머지 번호의 dpl권한은 kernel mode == 0 을 넣어줌
    SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0);

  // system call만 user mode 3의 값을 dpl에 저장 (DPL_USER == 1)
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3, vectors[T_SYSCALL], DPL_USER);

  initlock(&tickslock, "time");
}

void
idtinit(void)
{
  lidt(idt, sizeof(idt));
}

//PAGEBREAK: 41

// trapframe 구조체 -> trap이 호출될 때 프로세스 레지스터의 정보를 담고 있음. (x86.h 사용되는 모든 레지스터에 저장되어 있음)
// interrupt 후 다시 유저프로세스로 돌아갈 때 레지스터를 다시 복구하기 위해 (ex system call)
// or user process정보를 커널모드에서 사용하기 위해
void
trap(struct trapframe *tf)
{
  // trap이 호출되면 system call은 따로 처리를 해준다
  if(tf->trapno == T_SYSCALL){
    if(myproc()->killed)
      exit();
    myproc()->tf = tf;
    syscall();
    if(myproc()->killed)
      exit();
    return;
  }
  // 나머지는 switch문을 통해 해당되는 Interrupt 수행
  switch(tf->trapno){
  case T_IRQ0 + IRQ_TIMER:
    if(cpuid() == 0){
      acquire(&tickslock);
      ticks++;
      wakeup(&ticks);
      release(&tickslock);
    }
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE:
    ideintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE+1:
    // Bochs generates spurious IDE1 interrupts.
    break;
  case T_IRQ0 + IRQ_KBD:
    kbdintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_COM1:
    uartintr();
    lapiceoi();
    break;
  case T_IRQ0 + 7:
  case T_IRQ0 + IRQ_SPURIOUS:
    cprintf("cpu%d: spurious interrupt at %x:%x\n",
            cpuid(), tf->cs, tf->eip);
    lapiceoi();
    break;
  case 129:
    if(myproc()->killed)
      exit();
    myproc()->tf = tf;
    // schedulerLock 시스템 콜 수행
    schedulerLock(2017033654);    
    if(myproc()->killed)
      exit();
    break;  
  case 130:
    if(myproc()->killed)
      exit();
    myproc()->tf = tf;
    // schedulerUnlock 시스템 콜 수행
    schedulerUnlock(2017033654);    
    if(myproc()->killed)
      exit();
    break;

  //PAGEBREAK: 13
  default:
    if(myproc() == 0 || (tf->cs&3) == 0){
      // In kernel, it must be our mistake.
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpuid(), tf->eip, rcr2());
      panic("trap");
    }
    // In user space, assume process misbehaved.
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            myproc()->pid, myproc()->name, tf->trapno,
            tf->err, cpuid(), tf->eip, rcr2());
    myproc()->killed = 1;
  }

  // Force process exit if it has been killed and is in user space.
  // (If it is still executing in the kernel, let it keep running
  // until it gets to the regular system call return.)
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();

  // Force process to give up CPU on clock tick.
  // If interrupts were on while locks held, would need to check nlock.
  if(myproc() && myproc()->state == RUNNING && tf->trapno == T_IRQ0+IRQ_TIMER) {
    
    // Timer Interrupt 호출 시 마다
    global_ticks++;    
    myproc()->used_time++;
    yield();

  } 
  // Check if the process has been killed since we yielded
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();
}
