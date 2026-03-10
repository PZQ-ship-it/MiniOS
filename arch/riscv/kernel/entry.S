.section .text.entry
    .align 2
    .globl _traps 
_traps:

    csrr t0, sscratch
    # 如果是 内核线程( 没有 U-Mode Stack ) 触发了异常，则不需要进行切换。
    # （内核线程的 sp 永远指向的 S-Mode Stack， sscratch 为 0）
    beq t0, x0, _save_context
    # 保存栈指针
    csrw sscratch, sp
    mv sp, t0

    _save_context:
    # 1. save 32 registers and sepc to stack
    # why we need to save epc?
    add sp, sp, -(32 + 5)*8
    sd x0, 0(sp)
    sd x1, 8(sp)
    sd x2, 16(sp)
    sd x3, 24(sp)
    sd x4, 32(sp)
    sd x5, 40(sp)
    sd x6, 48(sp)
    sd x7, 56(sp)
    sd x8, 64(sp)
    sd x9, 72(sp)
    sd x10, 80(sp)
    sd x11, 88(sp)
    sd x12, 96(sp)
    sd x13, 104(sp)
    sd x14, 112(sp)
    sd x15, 120(sp)
    sd x16, 128(sp)
    sd x17, 136(sp)
    sd x18, 144(sp)
    sd x19, 152(sp)
    sd x20, 160(sp)
    sd x21, 168(sp)
    sd x22, 176(sp)
    sd x23, 184(sp)
    sd x24, 192(sp)
    sd x25, 200(sp)
    sd x26, 208(sp)
    sd x27, 216(sp)
    sd x28, 224(sp)
    sd x29, 232(sp)
    sd x30, 240(sp)
    sd x31, 248(sp)

    csrr t0, sepc
    sd t0, 256(sp)
    csrr t0, sstatus
    sd t0, 264(sp)
    csrr t0, stval
    sd t0, 272(sp)
    csrr t0, sscratch
    sd t0, 280(sp)
    csrr t0, scause
    sd t0, 288(sp)
   
    # 2. call trap_handler
    # 会读取scause寄存器和sepc寄存器的值，将它们和栈指针作为参数，调用trap_handler函数
    csrr a0, scause
    csrr a1, sepc
    mv a2, sp
    jal trap_handler
    # 先ld出来，因为后面可能有addi要修改sp的值
   
    # 3. restore sepc and 32 registers (x2(sp) should be restore last) from stack
    .global __ret_from_fork
     __ret_from_fork:
    ld t0, 256(sp)
    csrw sepc,t0
    ld x0, 0(sp)
    ld x1, 8(sp)

    ld x3, 24(sp)
    ld x4, 32(sp)
    ld x5, 40(sp)
    ld x6, 48(sp)
    ld x7, 56(sp)
    ld x8, 64(sp)
    ld x9, 72(sp)
    ld x10, 80(sp)
    ld x11, 88(sp)
    ld x12, 96(sp)
    ld x13, 104(sp)
    ld x14, 112(sp)
    ld x15, 120(sp)
    ld x16, 128(sp)
    ld x17, 136(sp)
    ld x18, 144(sp)
    ld x19, 152(sp)
    ld x20, 160(sp)
    ld x21, 168(sp)
    ld x22, 176(sp)
    ld x23, 184(sp)
    ld x24, 192(sp)
    ld x25, 200(sp)
    ld x26, 208(sp)
    ld x27, 216(sp)
    ld x28, 224(sp)
    ld x29, 232(sp)
    ld x30, 240(sp)
    ld x31, 248(sp)

    ld x2, 16(sp)

    add sp, sp, 37*8

    # 4. return from trap
    csrr t0, sscratch
    # 如果sscratch寄存器的值等于0，那么就跳转到_end标签处
    # 如果是 内核线程( 没有 U-Mode Stack ) 触发了异常，则不需要进行切换。
    # （内核线程的 sp 永远指向的 S-Mode Stack， sscratch 为 0）
    beq t0, x0, _end
    # # 恢复栈指针
    csrw sscratch, sp
    mv sp, t0
    _end:
    sret 


    

.global __dummy
__dummy:
# 处理从内核模式返回到用户模式的情况，保存和恢复栈指针
# 第一轮调度时访问
    csrr t0, sscratch
    csrw sscratch, sp
    mv sp, t0
    sret


# when entering __switch_to, a0 contains previous PCB address, a1 contains next PCB address to switch to 
.globl __switch_to
__switch_to:
    # get address of ra for both tasks (old & new)
    li a4, 32 # 5 * 8 bytes = 40 bytes,是存储task_struct前五个东西占的内存区段
    add a3, a0, a4  # a3 now points to old task's thread_struct
    add a4, a1, a4  # a4 now points to new task's thread_struct

   # save state to prev process
    sd ra, 0(a3)
    sd sp, 8(a3)
    sd s0, 16(a3)
    sd s1, 24(a3)
    sd s2, 32(a3)
    sd s3, 40(a3)
    sd s4, 48(a3)
    sd s5, 56(a3)
    sd s6, 64(a3)
    sd s7, 72(a3)
    sd s8, 80(a3)
    sd s9, 88(a3)
    sd s10, 96(a3)
    sd s11, 104(a3)

    # save sepc，sstatus，sscratch，satp 
    csrr t1, sepc
    sd t1, 112(a3)
    csrr t1, sstatus
    sd t1, 120(a3)
    csrr t1, sscratch
    sd t1, 128(a3)
    csrr t1, satp
    sd t1, 136(a3)

    # restore state from next process
    ld ra, 0(a4)
    ld sp, 8(a4)
    ld s0, 16(a4)
    ld s1, 24(a4)
    ld s2, 32(a4)
    ld s3, 40(a4)
    ld s4, 48(a4)
    ld s5, 56(a4)
    ld s6, 64(a4)
    ld s7, 72(a4)
    ld s8, 80(a4)
    ld s9, 88(a4)
    ld s10, 96(a4)
    ld s11, 104(a4)

    # restore sepc，sstatus，sscratch，satp
    # satp 实际上被放在了pgd中
    ld t2,112(a4)
    csrw sepc,t2
    ld t2,120(a4)
    csrw sstatus,t2
    ld t2,128(a4)
    csrw sscratch,t2
    ld t2,136(a4)
    csrw satp,t2

    sfence.vma zero, zero
    fence.i

    ret 
    # 线程第一次调度时，task[currentPID]->thread.ra = (uint64) __dummy;
    # 其他时候，返回到switch_to末尾

