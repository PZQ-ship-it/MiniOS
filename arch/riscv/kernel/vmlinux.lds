/* 目标架构 */
OUTPUT_ARCH( "riscv" )

/* 程序入口 */
ENTRY( _start )

PHY_START = 0x80000000;
PHY_SIZE =  (128 * 1024 * 1024);
PHY_END =   (PHY_START + PHY_SIZE);
PGSIZE = 0x1000;
OPENSBI_SIZE = (0x200000);
VM_START = (0xffffffe000000000);
VM_END =   (0xffffffff00000000);
VM_SIZE =  (VM_END - VM_START);
PA2VA_OFFSET = (VM_START - PHY_START);

MEMORY {
    ram  (wxa!ri): ORIGIN = PHY_START + OPENSBI_SIZE, LENGTH = PHY_SIZE - OPENSBI_SIZE
    ramv (wxa!ri): ORIGIN = VM_START + OPENSBI_SIZE, LENGTH = VM_SIZE - OPENSBI_SIZE
}

/* kernel代码起始位置 */
BASE_ADDR = VM_START + OPENSBI_SIZE;

SECTIONS
{
    /* . 代表当前地址 */
    . = BASE_ADDR;

    /* 记录kernel代码的起始地址 */
    _skernel = .;

    /* ALIGN(0x1000) 表示4KB对齐 */
    /* _stext, _etext 分别记录了text段的起始与结束地址 */
    .text : ALIGN(0x1000){
        _stext = .;

        *(.text.init)
        *(.text.entry)
        *(.text .text.*)
        
        _etext = .;
    } >ramv AT>ram

    .rodata : ALIGN(0x1000){
        _srodata = .;

        *(.srodata .srodata.*)
        *(.rodata .rodata.*)
        
        _erodata = .;
    } >ramv AT>ram
/*.data 是一个输出段，它包含了程序的初始化数据
ALIGN(0x1000) 是一个内置函数，用于将当前位置对齐到最近的 0x1000 字节边界。
这是为了确保数据段在内存中的位置满足某些硬件的对齐要求。*/
    .data : ALIGN(0x1000){
        /*设置了一个全局符号 _sdata，它的值是当前的输出位置（即数据段的开始位置）*/
        _sdata = .;
        /*将 .sdata 和 .data 输入段（以及它们的所有子段）放入当前的输出段*/
        *(.sdata .sdata*)
        *(.data .data.*)
        /*设置了一个全局符号 _edata，它的值是当前的输出位置（即数据段的结束位置）*/
        _edata = .;
        
        . = ALIGN(0x1000);
        /*设置了全局符号 ramdisk_start 和 ramdisk_end，它们的值分别是 .uapp 输入段的开始和结束位置*/
        ramdisk_start = .;
        *(.uapp .uapp*)
        ramdisk_end = .;
        . = ALIGN(0x1000);
    /*指定了输出段应该被放置在哪个内存区域。
    输出段被放置在名为 ramv 的内存区域
    加载地址（即程序被加载到内存时的实际地址）是 ram*/
    } >ramv AT>ram


    .bss : ALIGN(0x1000){
        _sbss = .;

        *(.bss.stack)
        *(.sbss .sbss.*)
        *(.bss .bss.*)
        
        _ebss = .;
    } >ramv AT>ram

    /* 记录kernel代码的结束地址 */
    _ekernel = .;
}