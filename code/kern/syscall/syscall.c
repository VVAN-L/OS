#include <unistd.h>
#include <proc.h>
#include <syscall.h>
#include <trap.h>
#include <stdio.h>
#include <pmm.h>
#include <assert.h>
#include <error.h>
#include <vmm.h>

static int sys_exit(uint64_t arg[]) { return do_exit((int)arg[0]); }
static int sys_fork(uint64_t arg[]) {
    return do_fork(0, current->tf->gpr.sp, current->tf);
}

static bool valid_buffer(uintptr_t addr, size_t len, bool write) {
    if (current->mm == NULL && !trap_in_kernel(current->tf))
        return 0;
    return user_mem_check(current->mm, addr, len, write);
}

static int sys_wait(uint64_t arg[]) {
    int *status = (int *)arg[1];
    if (status && !valid_buffer(arg[1], sizeof(*status), 1))
        return -E_INVAL;
    return do_wait((int)arg[0], status);
}

static int sys_exec(uint64_t arg[]) {
    if (!valid_buffer(arg[0], arg[1], 0) || !valid_buffer(arg[2], arg[3], 0))
        return -E_INVAL;
    return do_execve((const char *)arg[0], arg[1], (unsigned char *)arg[2], arg[3]);
}

static int sys_yield(uint64_t arg[]) { return do_yield(); }
static int sys_kill(uint64_t arg[]) { return do_kill((int)arg[0]); }
static int sys_getpid(uint64_t arg[]) { return current->pid; }
static int sys_putc(uint64_t arg[]) {
    cputchar((int)arg[0]);
    return 0;
}
static int sys_pgdir(uint64_t arg[]) {
    print_pgdir();
    return 0;
}


static int (*syscalls[])(uint64_t arg[]) = {
    [SYS_exit]              sys_exit,
    [SYS_fork]              sys_fork,
    [SYS_wait]              sys_wait,
    [SYS_exec]              sys_exec,
    [SYS_yield]             sys_yield,
    [SYS_kill]              sys_kill,
    [SYS_getpid]            sys_getpid,
    [SYS_putc]              sys_putc,
    [SYS_pgdir]             sys_pgdir,
};

#define NUM_SYSCALLS        ((sizeof(syscalls)) / (sizeof(syscalls[0])))

//lab5 function1 YOUR CODE
void syscall(void)
{
    struct pushregs *regs = &current->tf->gpr;
    uintptr_t num = regs->a0;
    if (num >= NUM_SYSCALLS || syscalls[num] == NULL) {
        regs->a0 = -E_UNIMP;
        return;
    }
    uint64_t args[] = {regs->a1, regs->a2, regs->a3, regs->a4, regs->a5};
    regs->a0 = syscalls[num](args);
}
