#include "stdint.h"
//系统调用号
#define SYS_WRITE   64
#define SYS_GETPID  172
#define SYS_CLONE    220 // fork
//常用于保存处理器的寄存器状态，以便在中断或异常处理程序中恢复处理器的状态
//传入sp寄存器的值，即栈顶指针
//按照偏移取出寄存器的值
struct pt_regs{
    uint64_t x[32];
    uint64_t sepc;
    uint64_t sstatus;
    uint64_t stval;
    uint64_t sscratch;
    uint64_t scause;
};

void syscall(struct pt_regs *reg);
uint64_t sys_clone(struct pt_regs *regs);