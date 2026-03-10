// arch/riscv/kernel/proc.c
#include "proc.h"
#include "printk.h"
#include "mm.h"
#include "rand.h"
#include "defs.h"
#include "test.h"
#include "elf.h"
extern void __dummy();

struct task_struct *idle;           // idle process
struct task_struct *current;        // 指向当前运行线程的 `task_struct`
struct task_struct *task[NR_TASKS]; // 线程数组, 所有的线程都保存在此



#ifdef DSJF
uint64 mintime;
uint64 zeroCount;
uint64 nextPID;
#endif

#ifdef DPRIORITY
uint64 maxcounter;
uint64 zeroCount;
uint64 nextPID;
#endif
extern void __switch_to(struct task_struct *prev, struct task_struct *next);
extern unsigned long swapper_pg_dir[512] __attribute__((__aligned__(0x1000)));
extern void create_mapping(uint64 *pgtbl, uint64 va, uint64 pa, uint64 sz, int perm);


void switch_to(struct task_struct *next)
{
    /* YOUR CODE HERE */
    // do nothing ifthe next process scheduled is the same as current one
    if (current == next)
        return;
    // jump to __switch_to if the next process scheduled is not the same as current one
    else if (current != next)
    {
        struct task_struct *old = current;
        current = next;                                                                                        // modify current
        printk("switch to [PID = %d COUNTER = %d PRIORITY = %d]\n", next->pid, next->counter, next->priority); // TEST
        __switch_to(old, next);                                                                                // jump to __switch_to
    }
}

void schedule(void) {
    /* YOUR CODE HERE */

    #ifdef DSJF

    mintime= 0xffffffffffffffff;
    zeroCount = 0;
    nextPID = 1;
    uint64 counter = 0;
    for(int i = 1; i < NR_TASKS; i++) {
        if(task[i]==NULL) continue;
        counter++;
        // if the remaining time is 0, increase zeroCount and NOT change shortestPID
         if (task[i]->counter == 0) {
            zeroCount++;
        // if remaining time > 0, judge if the shortestPID should be changed
        } else if (task[i]->counter < mintime) {
            nextPID = i;
            mintime = task[i]->counter;
        } 
    }
     // if the all tasks' running time are 0
    if (zeroCount == counter ) {
        for (int i = 1; i <=counter; i++) {
            task[i]->counter = rand();//reset running time for all tasks
            printk("SET [PID = %d PRIORITY = %d COUNTER = %d]\n",
                    task[i]->pid,
                    task[i]->priority,
                    task[i]->counter
                );//TEST
        }
        for(int i = 1; i < NR_TASKS; i++) {
        if(task[i]==NULL) continue;
        counter++;
        // if the remaining time is 0, increase zeroCount and NOT change shortestPID
         if (task[i]->counter == 0) {
            zeroCount++;
        // if remaining time > 0, judge if the shortestPID should be changed
        } else if (task[i]->counter < mintime) {
            nextPID = i;
            mintime = task[i]->counter;
        } 
    }
    }
    switch_to(task[nextPID]);//switch to next
    #endif

    #ifdef DPRIORITY
    maxcounter= 0;
    zeroCount = 0;
    nextPID = 1;
    uint64 counter = 0;
    for(int i = NR_TASKS - 1; i >= 1; i--) {
        // if the remaining time is 0, increase zeroCount and NOT change shortestPID
        if(task[i]==NULL) continue;
        counter++;

        if (task[i]->counter == 0) {
            zeroCount++;
        // if remaining time > 0, judge if the shortestPID should be changed
        } else if (task[i]->counter > maxcounter) {
            nextPID = i;
            maxcounter = task[i]->counter;
        } 
    }
     // if the all tasks' running time are 0
    if (zeroCount ==counter) {
        for (int i = 1; i <=counter; i++) {
            task[i]->counter = (task[i]->counter >> 1) + task[i]->priority;//reset running time for all tasks
            printk("SET [PID = %d PRIORITY = %d COUNTER = %d]\n",
                    task[i]->pid,
                    task[i]->priority,
                    task[i]->counter
                );//TEST
        }
        for(int i = NR_TASKS - 1; i >= 1; i--) {
        // if the remaining time is 0, increase zeroCount and NOT change shortestPID
        if(task[i]==NULL) continue;
        counter++;

        if (task[i]->counter == 0) {
            zeroCount++;
        // if remaining time > 0, judge if the shortestPID should be changed
        } else if (task[i]->counter > maxcounter) {
            nextPID = i;
            maxcounter = task[i]->counter;
        } 
    }
    }
    switch_to(task[nextPID]);//switch to next
    #endif
}
   


void do_timer(void)
{
    // 1. 如果当前线程是 idle 线程 直接进行调度
    if (current == idle)
    {
        schedule();
        // 2. 如果当前线程不是 idle
    }
    else
    {
        // 若剩余时间仍然大于0 则直接返回
        if (--current->counter > 0)
        {
            return; // 返回到trap_handler
            // 否则进行调度
        }
        else
        {
            schedule();
        }
    }
}

extern char ramdisk_start[];
extern char ramdisk_end[];

static uint64_t load_program(struct task_struct* task) {
    //Elf64_Ehdr是ELF文件的头部结构，包含了ELF文件的一些基本信息
    //定义了一个指向Elf64_Ehdr类型的指针ehdr，并将其指向用户应用程序的起始地址
   Elf64_Ehdr* ehdr = (Elf64_Ehdr*)ramdisk_start;
   //指向第一个phdr起始位置
    uint64_t phdr_start = (uint64_t)ehdr + ehdr->e_phoff;
    int phdr_cnt = ehdr->e_phnum;

    Elf64_Phdr* phdr;
    for (int i = 0; i < phdr_cnt; i++) {
        //指向第i个phdr起始位置
        phdr = (Elf64_Phdr*)(phdr_start + sizeof(Elf64_Phdr) * i);
        if(phdr->p_type != PT_LOAD){
            continue;
        }
        uint64_t flag = 0;
        // p_flags = RWX and vma->vm_flags are different.
        if(phdr->p_flags & PF_X){
            flag |= VM_X_MASK;
        }
        if(phdr->p_flags & PF_W){
            flag |= VM_W_MASK;
        } 
        if(phdr->p_flags & PF_R){
            flag |= VM_R_MASK;
        }
        //代码和数据区域：该区域从 ELF 给出的 Segment 起始地址 phdr->p_offset 开始，权限参考 phdr->p_flags 进行设置。
        //创建用户程序vma
        do_mmap(task, phdr->p_vaddr, phdr->p_memsz, flag, phdr->p_offset, phdr->p_filesz);
    }
    //设置用户程序的入口地址
    task->thread.sepc = ehdr->e_entry; 
}


void task_init()
{
    // 1. 调用 kalloc() 为 idle 分配一个物理页
    idle = (struct task_struct *)kalloc();
    // 2. 设置 state 为 TASK_RUNNING;
    idle->state = TASK_RUNNING;
    // 3. 由于 idle 不参与调度 可以将其 counter / priority 设置为 0
    idle->counter = 0;
    idle->priority = 0;
    // 4. 设置 idle 的 pid 为 0
    idle->pid = 0;
    // 5. 将 current 和 task[0] 指向 idle
    current = idle;
    task[0] = idle;
    /* YOUR CODE HERE */
   
    for(int i=1;i<2;i++){
        task[i]=(struct task_struct*)kalloc();
        task[i]->state=TASK_RUNNING;
        task[i]->counter=0;
        task[i]->priority=rand();
        task[i]->pid=i;
        task[i]->thread.ra=(uint64)(&__dummy);
        task[i]->thread.sp=(uint64)task[i]+PGSIZE;
        
        //为每个任务分配一个新的页目录表
        task[i]->pgd=(unsigned long*)kalloc();
        //复制页全局目录
        //swapper_pg_dir是系统的初始PGD
        //RISC-V的SV39分页模式下，一个PGD包含512个条目
        //每个条目都是一个页表的指针，它指向一个包含512个页表条目的页表
        //这段代码实际上是在复制整个页表结构，以便于后续的地址空间切换
        for(int j=0;j<512;j++){
           task[i]->pgd[j]=swapper_pg_dir[j];
        }
        load_program(task[i]);
        //调用 do_mmap 函数，建立用户 task 的虚拟地址空间信息vma
        //用户栈：范围为 [USER_END - PGSIZE, USER_END) ，权限为 VM_READ | VM_WRITE, 并且是匿名的区域
        do_mmap(task[i], USER_END - PGSIZE, PGSIZE, VM_W_MASK | VM_R_MASK | VM_ANONYM, 0, PGSIZE);
        //2.设置satp:原satp右移44再左移44清空ppn，保留ASID；
        //pgd减去偏移将页表基地址转化为物理地址；
        //再右移12清空offset，保留ppn
        //或在一起组成新的satp,存在pgd里
        task[i]->thread.satp = ((csr_read(satp) >> 44) << 44) | ((uint64_t)task[i]->pgd - PA2VA_OFFSET) >> 12;
        //当任务进入内核模式时，它将使用USER_END作为其内核栈的地址，即用户模式栈的顶部，以此隔离用户空间和内核空间
        task[i]->thread.sscratch=USER_END;
        //sstatus
        //spp[8]=0：当从内核模式返回时，处理器应该切换到用户模式
        //spie[5]=1 当从内核模式返回时，应该启用中断
        //sum[18]=1 允许用户模式的代码访问用户模式的内存
        task[i]->thread.sstatus=0x40020;
    
    }
    for(int i=2;i<NR_TASKS;i++){
        task[i] = NULL;
    }
    printk("...proc_init done!\n");
}
//支持对 vm_area_struct 的添加
//就是用传入的参数初始化一个 vm_area_struct，然后将其添加到 task_struct 的 vmas 数组中
void do_mmap(struct task_struct *task, uint64_t addr, uint64_t length, uint64_t flags,uint64_t vm_content_offset_in_file, uint64_t vm_content_size_in_file){
        struct vm_area_struct* vma=&(task->vmas[task->vma_cnt++]);
        vma->vm_start=addr;
        vma->vm_end=addr+length;
        vma->vm_flags=flags;
        vma->vm_content_offset_in_file=vm_content_offset_in_file;
        vma->vm_content_size_in_file=vm_content_size_in_file;
}
//支持对 vm_area_struct 的查找
struct vm_area_struct *find_vma(struct task_struct *task, uint64_t addr){
    for(int i=0;i<task->vma_cnt;i++){
        //传入的是stval,即发生缺页异常的地址，判断该地址是否在某个vma的范围内
        if(task->vmas[i].vm_start<=addr&&task->vmas[i].vm_end>=addr)
            return &(task->vmas[i]);
    }
    return NULL;
}



// void dummy()
// {
//     uint64 MOD = 1000000007;
//     uint64 auto_inc_local_var = 0;
//     int last_counter = -1;
//     while (1)
//     {
//         if ((last_counter == -1 || current->counter != last_counter) && current->counter > 0)
//         {
//             if (current->counter == 1)
//             {
//                 --(current->counter); // forced the counter to be zero if this thread is going to be scheduled
//             }                         // in case that the new counter is also 1，leading the information not printed.
//             last_counter = current->counter;
//             auto_inc_local_var = (auto_inc_local_var + 1) % MOD;
//             printk("[PID = %d] is running.  thread space begin at 0x%lx\n", current->pid, (uint64)current);
//         }
//     }
// }