// trap.c 
#include "clock.h"
#include "types.h"
#include "printk.h"
#include "proc.h"
#include "syscall.h"
#include "mm.h"
#include "defs.h"
//types of interrupt and exception code in scause
const int MACHINE_TIMER_INTERRUPT_CODE = 5;
const int ECALL_FROM_U_MODE = 8;
const int EXCEPTION_CODE_INSTRUCTION_FETCH = 12;
const int EXCEPTION_CODE_READ = 13;
const int EXCEPTION_CODE_WRITE = 15;
extern struct task_struct* current;        // 指向当前运行线程的 `task_struct`
extern char ramdisk_start[];
extern void create_mapping(uint64 *pgtbl, uint64 va, uint64 pa, uint64 sz, int perm);

void do_page_fault(struct pt_regs *regs) {    
    // 1. 通过 stval 获得访问出错的虚拟内存地址（Bad Address）
    uint64 stval=regs->stval;
    // 2. 通过 find_vma() 查找 Bad Address 是否在某个 vma 中
    struct vm_area_struct* vma = find_vma(current,stval);
    if(vma==NULL){
        //如果当前访问的虚拟地址在 VMA 中没有记录，即是不合法的地址，则运行出错
    }
    else{//如果当前访问的虚拟地址在 VMA 中存在记录，则进行相应的映射即可：
        // 3. 分配一个页，将这个页映射到对应的用户地址空间
        uint64 new_page=(uint64)kalloc();
        create_mapping((uint64*)((uint64_t)current->pgd),stval>>12<<12,new_page-PA2VA_OFFSET,PGSIZE,(vma->vm_flags)&(0b1110)|0x11);
        
        //  4. 通过 (vma->vm_flags | VM_ANONYM) 获得当前的 VMA 是否是匿名空间
        if(!(vma->vm_flags&VM_ANONYM)){//非匿名空间
        //说明访问的页是存在数据的，如访问的是代码，则需要从文件系统中读取内容，随后进行映射
            char* page=(char*)new_page;
        //通过计算偏移来计算发生缺页位置在文件中对应的位置
        //stval-vma->vm_start：发生缺页的地址在 vma 中的偏移（内存中）
        //vma->vm_content_offset_in_file：vma 在文件中的偏移（磁盘中）
        //ramdisk_start：磁盘上用户空间起始地址
            uint64 page_fault_in_file =  (uint64)ramdisk_start +vma->vm_content_offset_in_file+(stval-vma->vm_start);
        //消除页内偏移
            char* source=(char*)(page_fault_in_file>>12<<12);
        //将文件中的内容拷贝到新分配的页中
            for(int i=0;i<PGSIZE;i++){
                page[i]=source[i];
            }
        }
        else{
            //如果是匿名区域，那么开辟一页内存，然后把这一页映射到导致异常产生 task 的页表中。（前面已经做了）
        }
    }
}


void trap_handler(uint64 scause, uint64 sepc,struct pt_regs *regs) {
    printk("[Trap Handler] a trap happens: scause = %lx, sepc = %lx, stval = %lx, current->pid = %d from %s-mode\n",
        scause, sepc, regs->stval, current->pid,regs->sstatus & 0x100 ? "kernel" : "user");
    if ((scause >> 63) > 0) { // highest bit of scause = 1 => Interrupt
        uint64 interruptCode = (scause<<1)>>1; // get lower 32 bits of scause
        if (interruptCode == MACHINE_TIMER_INTERRUPT_CODE){// machine timer interrupt
            clock_set_next_event();
            do_timer();
        }
        else{
            printk("[S] Unhandled trap, ");
            printk("scause: %lx, ", scause);
            printk("sepc: %lx\n", regs->sepc);
            while (1);
        }
    }
    else  {// highest bit of scause = 0 => exception
        uint64 exceptionCode = scause; 
        if (exceptionCode == ECALL_FROM_U_MODE) {// ecall from user mode
            syscall(regs);
            regs->sepc+=4;
        }
        else if (exceptionCode == EXCEPTION_CODE_INSTRUCTION_FETCH) {// instruction fetch exception
            printk("[S] FETCH Page Fault! sepc=%lxstval=%lx\n",regs->sepc,regs->stval);
            do_page_fault(regs);
        }
        else if (exceptionCode == EXCEPTION_CODE_READ) {// read exception
            printk("[S] READ Page Fault! sepc=%lxstval=%lx\n",regs->sepc,regs->stval);
            do_page_fault(regs);
        }
        else if (exceptionCode == EXCEPTION_CODE_WRITE) {// write exception
            printk("[S] WRITE Page Fault! sepc=%lxstval=%lx\n",regs->sepc,regs->stval);
            do_page_fault(regs);
        }
        else{
            printk("[S] Unhandled exceptions, ");
            printk("scause: %lx, ", scause);
            printk("sepc: %lx\n", regs->sepc);
            while (1);
        }
    }
}


