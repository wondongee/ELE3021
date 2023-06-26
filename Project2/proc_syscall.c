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
int sys_exec2(void) {
    char *path, *argv[MAXARG];
    int i, a;
    uint uargv, uarg;

    if(argstr(0, &path) < 0 || argint(1, (int*)&uargv) < 0){
        return -1;
    }
    if(argint(2, &a) < 0) {
        return -1;
    }
    memset(argv, 0, sizeof(argv));
    for(i=0;; i++){
        if(i >= NELEM(argv))
            return -1;
        if(fetchint(uargv+4*i, (int*)&uarg) < 0)
            return -1;
        if(uarg == 0){
            argv[i] = 0;
            break;
        }
        if(fetchstr(uarg, &argv[i]) < 0)
            return -1;
    }
    return exec2(path, argv, a);
}

int sys_setmemorylimit(void) {
    int a, b;
    if(argint(0, &a) < 0) {
        return -1;
    }
    if(argint(1, &b) < 0) {
        return -1;
    }    
    return setmemorylimit(a, b);
}

int sys_list_process(void) {
    list_process();
    return 0;
}

int sys_thread_create(void) {
    int thread, routine, arg;

    if((argint(0, &thread) < 0) || (argint(1, &routine) < 0) || (argint(2, &arg) < 0))
        return -1;
    return thread_create((thread_t*)thread, (void*)routine, (void*)arg);
}

int sys_thread_exit(void) {
    int retval;

    if(argint(0, &retval) < 0)
        return -1;
    
    thread_exit((void*)retval);
    return 0;

}
int sys_thread_join(void) {
    int thread, retval;

    if((argint(0, &thread) < 0) || (argint(1, &retval) < 0))
        return -1;
    return thread_join((thread_t)thread, (void**)retval);
}

int sys_clear_thread(void) {
    clear_thread();    
    return 0;
}
