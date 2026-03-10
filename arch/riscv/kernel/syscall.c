#include "proc.h"
// #include "syscall.h"
#include "syscall.h"
#include "printk.h"
#include "mm.h"
#include "defs.h"
#include "rand.h"
extern struct task_struct* current; 
extern struct task_struct* task[NR_TASKS];
extern void __ret_from_fork();
extern unsigned long swapper_pg_dir[512] __attribute__((__aligned__(0x1000)));
extern void create_mapping(uint64 *pgtbl, uint64 va, uint64 pa, uint64 sz, int perm);
extern char ramdisk_start[];
extern char ramdisk_end[];

void syscall(struct pt_regs *regs) {
    if(regs->x[17] == SYS_GETPID) {
        //该调用从current中获取当前的pid放入a0中返回
        // RISC-V架构中，系统调用的返回值通常会被放在a0寄存器，也就是x[10]
        regs->x[10] = current->pid;
    }
    //sys_write
    // sys_write(unsigned int fd, const char* buf, size_t count)
    // 该调用将用户态传递的字符串打印到屏幕上
    //此处fd为要写入到标准输出（1），buf为用户需要打印的起始地址，count为字符串长度，返回打印的字符数
    else if (regs->x[17] == SYS_WRITE) {
        if (regs->x[10] == 1) { // fd
            // count
            uint64 end = regs->x[12];//count
            char *out = (char *)regs->x[11];//buf
            out[end] = '\0';//将输出字符串的末尾设置为\0
            regs->x[10] = printk(out);//将printk函数的返回值赋值给寄存器x[10]，作为系统调用的返回值
        }
    }
    else if(regs->x[17]==SYS_CLONE) {
        regs->x[10]=sys_clone(regs);//父进程返回子进程的pid
    }
    
}

uint64_t sys_clone(struct pt_regs *regs) {
    /*
     1. 参考 task_init 创建一个新的 task, 将的 parent task 的整个页复制到新创建的 
        task_struct 页上(这一步复制了哪些东西?）。将 thread.ra 设置为 
        __ret_from_fork, 并正确设置 thread.sp
        (仔细想想，这个应该设置成什么值?可以根据 child task 的返回路径来倒推)

     2. 利用参数 regs 来计算出 child task 的对应的 pt_regs 的地址，
        并将其中的 a0, sp, sepc 设置成正确的值(为什么还要设置 sp?)

     3. 为 child task 申请 user stack, 并将 parent task 的 user stack 
        数据复制到其中。 
        
     3.1. 同时将子 task 的 user stack 的地址保存在 thread_info->
        user_sp 中，如果你已经去掉了 thread_info，那么无需执行这一步

     4. 为 child task 分配一个根页表，并仿照 setup_vm_final 来创建内核空间的映射

     5. 根据 parent task 的页表和 vma 来分配并拷贝 child task 在用户态会用到的内存

     6. 返回子 task 的 pid
    */
   // 1. 调用 kalloc() 为 idle 分配一个物理页
 

    for(int i=1;i<NR_TASKS;i++){
        if(task[i] == NULL){//找到一个未初始化的task
            //创建并拷贝进程控制块
            task[i] = (struct task_struct *)kalloc();
            char *dst = (char *)task[i];
            char *src = (char *)current;
            for(uint64_t j = 0; j < PGSIZE; j++){
                dst[j] = src[j];
            }
            //利用参数 regs 来计算出 child task 的对应的 pt_regs 的地址
            //child_regs - task[i] = regs - current 利用偏移相等
            struct pt_regs *child_regs=(struct pt_regs*)((uint64)task[i]+(uint64)regs-(uint64)current);
            //子进程sepc+4,父进程的sepc在发生异常时会自动+4，此处无需+4
            child_regs->sepc=child_regs->sepc+4;
            //同理，child_regs->sp - task[i] = regs->sp - current
            child_regs->x[2]=child_regs->x[2] -(uint64)current+(uint64)task[i];
            //子进程返回pid = 0
            child_regs->x[10]=0;  
            task[i]->state=TASK_RUNNING;
            task[i]->counter=0;
            task[i]->priority=rand();
            task[i]->pid = i;
            //设置ra为__ret_from_fork，下次调度时会从__ret_from_fork开始执行，将栈里的寄存器恢复，再返回到用户态
            task[i]->thread.ra = (uint64)(&__ret_from_fork);
            task[i]->thread.sp=child_regs->x[2];
            //pt_regs
            task[i]->thread.sepc=child_regs->sepc;
            task[i]->thread.sscratch=child_regs->sscratch;
            task[i]->thread.sstatus=child_regs->sstatus;
            //创建并拷贝页表
            task[i]->pgd=(unsigned long*)kalloc();
            for(int j=0;j<512;j++){
                task[i]->pgd[j]=swapper_pg_dir[j];
            }
            //设置satp（同task_init）
            task[i]->thread.satp = ((csr_read(satp) >> 44) << 44) | ((uint64_t)task[i]->pgd - PA2VA_OFFSET) >> 12;
            //根据父进程的页表和vma来分配并拷贝子进程在用户态会用到的内存
            for(int j=0;j<current->vma_cnt;j++){//遍历父进程的vma
                //
                struct vm_area_struct* vma=&(task[i]->vmas[j]);
                //取出父进程的vma的信息，用于拷贝和创建映射
                uint64 vm_start=vma->vm_start,vm_end=vma->vm_end;
                uint64 vm_size = vm_end -vm_start;
                //计算起止页号
                uint64 page_start = vm_start>>12;
                uint64 page_end = (vm_end-1)>>12;
                //设置权限：取出父进程的vma的读、写、执行权限，再将存在与用户态访问权限置1
                uint64 perm = 0b1110;
                perm = (perm&vma->vm_flags)|0b10001;
                //分配页
                uint64_t pa = alloc_pages(page_end -page_start +1);
                //拷贝
                char *dst_vm = (char *)pa;
                char *src_vm = (char *)vm_start;
                for(uint64_t j = 0; j < vm_size; j++){
                    dst_vm[j] = src_vm[j];
                }
                printk("create_mapping running!\n");
                //创建映射
                create_mapping(task[i]->pgd,vm_start,pa-PA2VA_OFFSET,vm_size,perm);
               
            }
            //父进程返回子进程的pid
            return i;

        }
    }

}