# MiniOS - RISC-V 教学操作系统

MiniOS 是一个运行在 RISC-V 64 位架构上的简易操作系统。项目基于 QEMU 虚拟机和 RISC-V 交叉编译工具链，从零实现了内核启动、进程管理、调度算法、内存管理、异常与中断处理以及基本的系统调用和用户态程序加载。

## 功能特性概览

- **平台与运行环境**
  - 目标架构：`rv64imafd_zifencei`，ABI：`lp64`
  - 在 QEMU `virt` 机器上运行，使用 `qemu-system-riscv64 -nographic`
  - 使用 RISC-V 交叉编译工具链 `riscv64-linux-gnu-*`

- **启动流程**
  - 启动代码：`arch/riscv/kernel/entry.S`、`head.S`
  - C 入口：`init/main.c` 中的 `start_kernel`
  - 内核启动后初始化调度与测试逻辑：
    ```c
    int start_kernel() {
        schedule();
        test(); // DO NOT DELETE !!!
        return 0;
    }
    ```

- **进程与调度**
  - 进程控制块：`struct task_struct`，全局数组 `task[NR_TASKS]` 保存所有任务
  - `proc.c` 中实现：
    - `task_init()`：初始化 idle 进程与第一个用户进程，设置页表、栈、寄存器状态
    - `switch_to()`：内核线程切换，调用汇编实现 `__switch_to`
    - `schedule()`：根据不同宏选择调度算法
  - 支持的调度策略（通过编译选项选择）：
    - `DSJF`：最短作业优先（Shortest Job First）
    - `DPRIORITY`：基于优先级的时间片轮转/多级反馈（在顶层 `Makefile` 中默认 `-D DPRIORITY`）

- **内存管理**
  - 物理内存分配：
    - 在 `arch/riscv/kernel/mm.c` 中实现伙伴系统（Buddy System）
    - `buddy_init()` 初始化伙伴系统，管理以页为单位的物理内存
    - `alloc_page()/alloc_pages()` 与 `free_pages()` 提供页级分配与释放
    - `kalloc()/kfree()` 封装为内核分配接口
  - 虚拟内存与页表：
    - 通过 `vm.c` 和 `create_mapping()` 等函数维护 Sv39 页表
    - 每个进程拥有独立的 `pgd`（根页表），fork 时复制内核空间映射
  - 缺页异常处理：
    - `trap.c` 中 `do_page_fault()` 根据 `stval` 和 `find_vma()` 定位 VMA
    - 支持匿名映射（栈等）与文件映射（代码段、数据段），按需分配物理页并建立映射

- **中断与异常**
  - 时钟中断：
    - `clock.c` 中使用 `rdtime` 读取时间，并通过 `sbi_ecall` 设置下一次时钟中断
    - `TIMECLOCK = 10000000`，即默认 1 秒触发一次时钟中断（QEMU 下 10MHz）
  - trap 统一入口：
    - `trap_handler()` 处理：
      - 机器/监督模式时钟中断 -> `do_timer()` -> `schedule()`
      - 用户态 `ecall` -> 系统调用分发 `syscall()`
      - 指令取指/读/写引起的 Page Fault -> `do_page_fault()`
  - 通过打印 `scause`、`sepc`、`stval` 等信息，便于调试异常与中断逻辑

- **系统调用与用户态支持**
  - 系统调用入口：`arch/riscv/kernel/syscall.c` 中的 `syscall(struct pt_regs *regs)`
  - 已实现的主要系统调用：
    - `SYS_GETPID`：获取当前进程 PID
    - `SYS_WRITE`：向标准输出（fd == 1）打印字符串
    - `SYS_CLONE`：创建子进程（类似简化版 `fork`）
  - `sys_clone()` 主要步骤：
    - 为子进程分配 `task_struct` 页并拷贝父进程 PCB
    - 调整 pt_regs，使子进程从 `__ret_from_fork` 返回，并在用户态从正确位置继续执行
    - 分配并拷贝用户栈与用户态地址空间（根据父进程 VMA 逐段复制，重建页表映射）
  - 用户态程序：
    - 位于 `user/` 目录，如 `getpid.c`、`printf.c` 等
    - 通过 `user/link.lds` 链接为 ELF，可由内核的 `load_program()` 从内存盘加载到用户空间

## 目录结构说明

- `arch/riscv/`
  - `kernel/`
    - `entry.S` / `head.S`：RISC-V 启动与特权级切换相关汇编
    - `proc.c`：进程初始化、调度与上下文切换
    - `mm.c`：物理内存分配（Buddy System）与 `kalloc/kfree`
    - `trap.c`：异常与中断统一入口，Page Fault 与系统调用处理
    - `clock.c`：时钟中断与 `rdtime` 封装
    - `sbi.c`：SBI 调用封装
    - `vm.c`：页表与虚拟内存相关操作
    - `vmlinux.lds`：内核链接脚本
  - `include/`：RISC-V 架构相关头文件（如 `defs.h`、`mm.h`、`proc.h`、`syscall.h` 等）

- `init/`
  - `main.c`：内核 C 入口 `start_kernel`
  - `test.c`：课程/实验自测用代码
  - `Makefile`：内核入口部分的编译规则

- `lib/`
  - 基础库函数实现，如 `printk.c`、`string.c`、`rand.c` 等
  - 提供简单的格式化输出与字符串/随机数等运行时支持

- `user/`
  - 用户态程序与运行时库（`getpid.c`、`printf.c`、`start.S`、`syscall.h` 等）
  - `link.lds`：用户态程序链接脚本

- `include/`
  - 平台无关的公共头文件：`elf.h`、`printk.h`、`rand.h`、`string.h`、`types.h` 等

- 顶层文件
  - `Makefile`：顶层构建脚本，编译 lib、user、init、arch/riscv 并链接得到 `vmlinux`
  - `.vscode/`：VS Code 配置（如编译/调试任务等）

## 编译与运行

### 1. 环境依赖

在编译和运行 MiniOS 之前，需要安装：

- RISC-V 交叉编译工具链（以 `riscv64-linux-gnu-` 为前缀）：
  - `riscv64-linux-gnu-gcc`
  - `riscv64-linux-gnu-ld`
  - `riscv64-linux-gnu-objcopy`
- QEMU（支持 RISC-V 64 位）：
  - `qemu-system-riscv64`
- `make`

> 建议在 Linux 或 WSL 环境下编译运行；在 Windows 原生环境中需要确保上述工具链已经正确安装并在 `PATH` 中。

### 2. 编译内核与用户程序

在 MiniOS 工程根目录执行：

```bash
make        # 或 make all
```

顶层 `Makefile` 会依次：

- 在 `lib/` 目录构建基础库
- 在 `user/` 目录构建用户程序
- 在 `init/` 目录构建内核入口相关对象
- 在 `arch/riscv/` 目录构建内核主体并链接生成 `vmlinux`

编译成功后，会在根目录看到 `vmlinux` 文件。

### 3. 运行 MiniOS

在根目录执行：

```bash
make run
```

等价于：

```bash
qemu-system-riscv64 -nographic -machine virt -kernel vmlinux -bios default
```

- 采用 `-nographic`，所有输出将通过终端串口打印
- 若能看到 MiniOS 的启动信息和测试输出，说明运行成功

### 4. 调试模式运行

若需要使用 GDB 调试，在根目录执行：

```bash
make debug
```

这会启动 QEMU 并监听 GDB 端口：

```bash
qemu-system-riscv64 -nographic -machine virt -kernel vmlinux -bios default -S -s
```

然后在另一个终端中使用 `riscv64-linux-gnu-gdb` 连接，例如：

```bash
riscv64-linux-gnu-gdb vmlinux
(gdb) target remote :1234
(gdb) layout regs
(gdb) b start_kernel
(gdb) c
```

即可单步调试 MiniOS 的启动和运行过程。

### 5. 清理编译产物

```bash
make clean
```

顶层 `Makefile` 会递归清理由各子目录生成的目标文件和 `vmlinux` 等文件。

## 学习与阅读建议

如果你是第一次接触本项目，建议按照下面顺序阅读源码：

1. **启动与整体流程**
   - `init/main.c`：了解 `start_kernel()` 做了什么
   - `arch/riscv/kernel/head.S`、`entry.S`：理解从 QEMU 加载到 C 入口的切换

2. **进程与调度**
   - `arch/riscv/kernel/proc.c` 中的 `task_init()`、`schedule()`、`switch_to()`
   - 尝试在 `DSJF` 和 `DPRIORITY` 两种调度策略之间切换，观察日志变化

3. **内存管理**
   - `arch/riscv/kernel/mm.c`：Buddy 分配器与 `kalloc/kfree`
   - `vm.c` 与 `do_page_fault()`：虚拟内存映射与按需分配

4. **系统调用与用户程序**
   - `arch/riscv/kernel/syscall.c`：理解 `SYS_GETPID`、`SYS_WRITE`、`SYS_CLONE` 的实现
   - `user/` 下的简单程序：体会从用户态发起 `ecall` 到内核处理再返回的完整路径

5. **中断与时钟**
   - `arch/riscv/kernel/clock.c`：计时器与下一次时钟中断设置
   - `trap.c`：统一 trap 入口以及 scause 的分类处理

