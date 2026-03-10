.extern start_kernel
.extern task_init
.extern mm_init
.extern clock_set_next_event
.extern early_pgtbl

    .section .text.init
    .globl _start

_start:
    # set sp
    la sp, boot_stack_top
    li t0, 0xffffffdf80000000
    sub sp, sp, t0
    add s0, sp, x0

    # init virtual memory
    # 在系统早期阶段，我们需要建立一个等值映射，使得虚拟地址可以直接映射到相同的物理地址
    call setup_vm
    # 将内核代码从加载地址移动到它实际运行的地址
    call relocate

    # init mm
    jal ra, mm_init

    # set vm final 
    jal ra, setup_vm_final

    # init task_struct
    jal ra, task_init

    # set first time interrupt
    # 不再等待一个时间片后进行调度，更改为 OS boot 完成之后立即调度 uapp 运行
    jal ra, clock_set_next_event

    # set stvec = _traps
    la t0, _traps
    csrw stvec, t0 

    # set sie[STIE] = 1, enable Time Interrupt
    addi t0, zero, 32
    # cannot use csrrsi here since the uimm field only supports 5 bits
    csrrs x0, sie, t0

    # set sstatus[SIE] = 1, enable Interrupts in S mode
    # 确保 schedule 过程不受中断影响
    # csrrsi x0, sstatus, 0x2
    
    jal ra, start_kernel
# 将内核代码从加载地址移动到它实际运行的地址
relocate:
    # set ra = ra + PA2VA_OFFSET
    # set sp = sp + PA2VA_OFFSET (If you have set the sp before)
    li t0, 0xffffffdf80000000 # PA2VA_OFFSET
    add ra, ra, t0
    add sp, sp, t0

    # set satp with early_pgtbl
    # set MODE SV39
    addi t2, x0, 8
    # 设置高4位的MODE字段
    slli t2, t2, 60
    # set PPN
    la t1, early_pgtbl
    # 减去PA2VA_OFFSET
    sub t1, t1, t0
    # 右移12位，得到PPN
    srli t1, t1, 12
    # 将PPN放到satp的低44位，MODE放到高4位
    add t1, t1, t2
    csrw satp, t1 # 设置新的页表

    # flush tlb（Translation Lookaside Buffer） 因为我们刚刚修改了页表
    sfence.vma zero, zero

    # flush icache 刷新指令缓存，因为我们刚刚修改了代码的地址
    fence.i

    ret

    .section .bss.stack
    .globl boot_stack

boot_stack:
    .space 4096 # stack size = 4KB
    .globl boot_stack_top

boot_stack_top: