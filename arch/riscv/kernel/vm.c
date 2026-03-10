// arch/riscv/kernel/vm.c
#include "mm.h"
#include "defs.h"
#include "string.h"
#include "printk.h"

/* early_pgtbl: 用于 setup_vm 进行 1GB 的 映射。 */
unsigned long  early_pgtbl[512] __attribute__((__aligned__(0x1000)));
/* swapper_pg_dir: kernel pagetable 根目录， 在 setup_vm_final 进行映射。 */
unsigned long  swapper_pg_dir[512] __attribute__((__aligned__(0x1000)));
void create_mapping(uint64 *pgtbl, uint64 va, uint64 pa, uint64 sz, int perm);
extern uint64 _stext,_etext,_srodata,_erodata,_sbss,_sdata,_ekernel;

/* early_pgtbl: 用于 setup_vm 进行 1GB 的 映射。 */
unsigned long early_pgtbl[512] __attribute__((__aligned__(0x1000)));
void setup_vm(void) {
    printk("setup_vm\n");//test
    //1. 由于是进行 1GB 的映射 这里不需要使用多级页表 
    memset(early_pgtbl, 0, PGSIZE);
    //要把va的地址映射到pa上（直接映射）
    unsigned long va = 0x80000000;
    unsigned long pa = va;
    //2. 将 va 的 64bit 作为如下划分： | high bit | 9 bit | 30 bit |
    //   high bit 可以忽略
    //  中间9 bit 作为 early_pgtbl 的 index
    // 0x0000007fc0000000 是一个位掩码，用于选取虚拟地址的中间 9 位
    uint64 index = (va&0x0000007fc0000000)>>30;

    // 低 30 bit 作为 页内偏移 
    //这里注意到 30 = 9 + 9 + 12， 即我们只使用根页表， 根页表的每个 entry 都对应 1GB 的区域。 
    /*实际上在这段代码中只使用了一级页表。这是因为这段代码是在进行 1GB 的映射，
    而在 Sv39 地址转换模式中，一级页表的每个条目都可以映射 1GB 的内存区域*/
    //物理页的大小为 4KB，ppn为物理页号,ppn=pa>>12,低12位为偏移量
    //pte中ppn在第10位，<<10
    unsigned long entry = (pa>>12)<<10;
    //3. Page Table Entry 的权限 V | R | W | X 位设置为 1
    entry|=0xF;
    //4. 将 entry 写入 early_pgtbl中，放在index起始位置
    early_pgtbl[index]=entry;

    //PA + PV2VA_entry == VA linear mapping
    va = 0xffffffe000000000;
    index = (va&0x0000007fc0000000)>>30;
    entry = (pa>>12)<<10;
    entry|=0xF;
    early_pgtbl[index]=entry;
    printk("setup_vm done!\n");//test
    return;
}
void setup_vm_final(void) {
    memset(swapper_pg_dir, 0x0, PGSIZE);

    // No OpenSBI mapping required

    // mapping kernel text X|-|R|V 内核的文本段
    // 第一个参数是 swapper_pg_dir，这是根页表的基地址
    // 第二个参数是虚拟地址，这里是 &_stext，即内核代码段的起始地址
    // 第三个参数是物理地址，这里是 &_stext-PA2VA_OFFSET
    // 第四个参数是映射的大小，这里是 &_etext - &_stext
    // 第五个参数是权限，这里是 11，即 X|-|R|V 1011
    create_mapping(swapper_pg_dir,(uint64)(&_stext),(uint64)(&_stext)-PA2VA_OFFSET,(uint64)(&_etext)-(uint64)(&_stext),11);

    // mapping kernel rodata -|-|R|V 只读数据段
    create_mapping(swapper_pg_dir,(uint64)(&_srodata),(uint64)(&_srodata)-PA2VA_OFFSET,(uint64)(&_erodata)-(uint64)(&_srodata),3);
    
    // mapping other memory -|W|R|V 其他内存区域
    //映射大小是数据段起始地址到物理内存结束地址的大小
    create_mapping(swapper_pg_dir,(uint64)(&_sdata),(uint64)(&_sdata)-PA2VA_OFFSET,(uint64)(PHY_END+PA2VA_OFFSET)-(uint64)(&_sdata),7);
    
    // set satp with swapper_pg_dir
    uint64 dir = (uint64)swapper_pg_dir-PA2VA_OFFSET;

    __asm__ volatile (
			"add t0,x0, %[dir]\n"
            //SET MODE SV39
			"addi t1, x0, 0x8\n"
			"slli t1,t1,60\n"
            //GET PPN
			"srli t0,t0, 12\n"
            //add PPN and MODE to satp
			"add t1, t1, t0\n"
			"csrw satp, t1\n"
			:: [dir]"r"(dir)
            :"memory"
   		 );


    // flush TLB
    asm volatile("sfence.vma zero, zero");
  
    // flush icache
    asm volatile("fence.i");
    return;
}


/* 创建多级页表映射关系 */
/*当CPU访问一个虚拟地址时，
会将虚拟地址的VPN部分用作页表的索引，从页表中查找对应的PPN，
然后将PPN和虚拟地址的偏移量组合起来，得到物理地址。
因此，页表中需要存储的是PPN，而不是VPN*/
void create_mapping_mine(uint64 *pgtbl, uint64 va, uint64 pa, uint64 sz, int perm) {
    /*
    pgtbl 为根页表的基地址
    va, pa 为需要映射的虚拟地址、物理地址
    sz 为映射的大小
    perm 为映射的读写权限

    创建多级页表的时候可以使用 kalloc() 来获取一页作为页表目录
    可以使用 V bit 来判断页表项是否存在
    */
   //不再进行等值映射，映射到高地址 PA + PV2VA_entry == VA

   //跟踪已经映射的内存大小
    uint64 have_allocate = 0;
    while(sz>have_allocate){//还有未映射的内存
        //页表存放pte，根页表是vpn[2]
        uint64* page2;
        //计算虚拟地址的第一级页表索引
        uint64 index1 = (va&0x0000007fc0000000)>>30;
        if(pgtbl[index1]&1){//存在
            //do nothing
        }
        else{//不存在，分配一个新的页，并将其地址写入页表项
        //kalloc 函数分配的是物理内存，但返回的地址是虚拟地址
            page2 = (uint64*)kalloc();
            //将页的虚拟地址转换为物理地址
            //右移 12 位得到物理页号（PPN）
            //左移 10 位留出页表项的最低 10 位，用于存储权限位和状态位
            uint64 entry1 = ((uint64)page2-PA2VA_OFFSET)>>12<<10;
            //将页表项的 V bit（也就是最低位）设置为 1，表示这个页表项是有效的
            entry1+=1;
            pgtbl[index1]=entry1;
        }
        uint64* page3;
        //计算虚拟地址的第二级页表索引
        uint64 index2 = (va&0x000000003fe00000)>>21;
        if(page2[index2]&1){
            //
        }
        else{
            page3 = (uint64*)kalloc();
            uint64 entry2 = ((uint64)page3-PA2VA_OFFSET)>>12<<10;
            entry2+=1;
            page2[index2]=entry2;
        }
        //计算虚拟地址的第三级页表索引
        uint64 index3 = (va&0x00000000001ff000)>>12;
        //直接创建对应的页表项，存的是物理页PPN
        uint64 entry3 = pa>>12<<10;
        //加上权限位
        entry3+=perm;
        page3[index3] = entry3;
        
        //更新va和pa以及已经映射的内存大小
        va +=  PGSIZE;
        pa +=  PGSIZE;
        have_allocate+=PGSIZE;
    }
}

// ---------------------------------------

// it just get pgtbl 2/1, not 0
// + is super than <<
uint64 *get_next_pgtbl_base(uint64 *pgtbl, uint64 VPN){
    uint64 next_pgtbl;
    // V = 1 
    // no priority control
    if(pgtbl[VPN] % 2){
        next_pgtbl = pgtbl[VPN] >> 10 << 12;
    }else{
        next_pgtbl = kalloc() - PA2VA_OFFSET;
        // pgn = address >> 12
        pgtbl[VPN] = (next_pgtbl >> 12 << 10) + 1;
    }
    // here return the virtual address
    return (uint64 *)(next_pgtbl + PA2VA_OFFSET);
}

/* 创建多级页表映射关系 */
// 这是一个只支持sz = PGSIZE的
void create_mapping_page(uint64 *pgtbl2, uint64 va, uint64 pa, uint64 sz, uint64 perm) {
    /*
    pgtbl 为根页表的基地址
    va, pa 为需要映射的虚拟地址、物理地址
    sz 为映射的大小
    perm 为映射的读写权限

    创建多级页表的时候可以使用 kalloc() 来获取一页作为页表目录
    可以使用 V bit 来判断页表项是否存在
    */
    // for sz < PGSIZE
    uint64 VPN2 = va << 25 >> 25 >> 30;
    VPN2 = va >> 30 & 0x1ff;
    uint64 VPN1 = va << 34 >> 34 >> 21;
    VPN1 = va >> 21 & 0x1ff;
    uint64 VPN0 = va << 43 >> 43 >> 12;
    VPN0 = va >> 12 & 0x1ff;
    uint64 VOFF = va << 52 >> 52;
    VOFF = va & 0xfff;
    uint64 PPN = pa >> 12;
    uint64 POFF = pa << 52 >> 52;
    POFF = pa & 0xfff;
    if (sz != PGSIZE){
        printk("ERROR: sz != PGSIZE, sz = %d\n", sz);
        return;
    }
    if (VOFF != POFF){
        printk("ERROR: VOFF != POFF\n");
    }

    // here if you wanna use creating mapping after setvm_final
    // you should use virtual address instead of phisical address
    uint64 *pgtbl1 = get_next_pgtbl_base(pgtbl2, VPN2);
    // if (perm == 0b11111)
    //     printk("%lx, pgtbl2[%ld]: %lx, pn = %lx\n", pgtbl2, VPN2, pgtbl2[VPN2], pgtbl2[VPN2] >> 10 << 12);
    uint64 *pgtbl0 = get_next_pgtbl_base(pgtbl1, VPN1);
    // if (perm == 0b11111)
    //     printk("%lx, pgtbl1[%lx]: %lx, pn = %lx\n", pgtbl1, VPN1, pgtbl1[VPN1], pgtbl1[VPN1] >> 10 << 12);

    // map PPN
    pgtbl0[VPN0] = (PPN << 10) + perm;
    // if (perm == 0b11111)
    //     printk("pgtbl0 = %lx, PPN = %lx\n", pgtbl0, pgtbl0[VPN0]>>10<<12);
}

void create_mapping_test(uint64 *pgtbl, uint64 va, uint64 pa, uint64 sz, uint64 perm){
    uint64_t pa_curr = pa;
    for(uint64_t page = va >> 12; page <= (va + sz - 1) >> 12; page++){
        create_mapping_page(pgtbl, page << 12, pa_curr, PGSIZE, perm);
        pa_curr += PGSIZE;
    }
}

void create_mapping(uint64 *pgtbl, uint64 va, uint64 pa, uint64 sz, int perm){
    create_mapping_test(pgtbl, va, pa, sz, perm);
}
