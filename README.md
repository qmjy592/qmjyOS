# qmjyOS — Experimental x86_64 Operating System

**qmjyOS** is an experimental hobby operating-system project written primarily in freestanding C and x86 assembly. It explores the mechanisms that connect a boot protocol, processor-mode transitions, kernel initialization, descriptor tables, interrupts, physical memory management, virtual memory, and task scheduling.

**qmjyOS** 是一个实验性质的业余操作系统项目，主要使用 freestanding C 与 x86 汇编编写。项目通过实际构建内核，探索引导协议、处理器模式切换、内核初始化、描述符表、中断、物理内存管理、虚拟内存以及任务调度之间的关系。

> **Status / 状态:** Experimental and under active development. Not production-ready and not security-audited.
>
> 本项目仍处于实验和持续开发阶段，不适合生产环境，也尚未经过完整安全审计。

## Documentation / 文档导航

- [Project Overview / 项目完整介绍](PROJECT_OVERVIEW_CN_EN.md)
- [Architecture Guide / 系统架构详解](ARCHITECTURE_CN_EN.md)
- [Build and Testing / 构建与测试](BUILD_AND_TESTING_CN_EN.md)

## Implemented components in the current source tree / 当前源码包含的模块

- Multiboot v1 32-bit bootstrap and a transition into a 64-bit kernel image.
- Early identity mapping of the first 1 GiB with 2 MiB pages.
- 64-bit entry, stack setup, BSS clearing and C entry.
- GDT reload, TSS descriptor construction and kernel privilege-transition stack setup.
- IDT infrastructure, exception stubs and serial panic diagnostics.
- 8259 PIC remapping, PIT programming, IRQ0/IRQ1 entry and dispatch.
- Multiboot memory-map parser and early physical-page allocator.
- An independent inactive 4 KiB page-table manager with map/unmap/query/self-test interfaces.
- Two-task cooperative Ring 0 scheduler and assembly context switch.
- Host-side tests for the physical memory allocator.

- Multiboot v1 32 位引导程序，以及进入 64 位内核映像的切换流程。
- 使用 2 MiB 页建立前 1 GiB 的早期恒等映射。
- 64 位入口、栈设置、BSS 清零和 C 语言入口。
- GDT 重新加载、TSS 描述符构造和特权级切换所需的内核栈设置。
- IDT 基础设施、异常入口桩和串口 panic 诊断。
- 8259 PIC 重映射、PIT 编程、IRQ0/IRQ1 入口与分发。
- Multiboot 内存映射解析器和早期物理页分配器。
- 独立且尚未激活的 4 KiB 页表管理器，提供映射、解除映射、查询和自测试接口。
- 双任务 Ring 0 协作式调度器和汇编上下文切换。
- 物理内存分配器的主机侧测试。

## Important implementation boundaries / 重要实现边界

The bootstrap currently maps the first 1 GiB with 2 MiB pages. This is not yet a complete per-process address-space design. The page-table manager maintains a separate static table pool and does not activate its root by writing CR3. Its self-test therefore tests page-table data structures without switching the live address space.

引导阶段目前使用 2 MiB 页映射前 1 GiB。这还不是完整的进程地址空间设计。分页管理器维护独立的静态页表池，不会通过写入 CR3 激活其根页表。因此，它的自测试验证的是页表数据结构，而不是实际切换运行中的地址空间。

The current kernel main path initializes the cooperative scheduler and enters its Ring 0 demo tasks. The PIT interrupt increments a tick counter; it does not preempt tasks or switch contexts. The Ring 3 test is explicitly disabled in the current main path because page permissions are not ready.

当前内核主流程会初始化协作式调度器并进入 Ring 0 演示任务。PIT 中断只增加 tick 计数，不会抢占任务或切换上下文。由于页权限尚未准备好，当前主流程明确禁用了 Ring 3 测试。

The exception handler reports diagnostic information and halts the kernel. It is not a user-process fault recovery mechanism. The keyboard IRQ path currently acknowledges the interrupt but does not implement a keyboard driver.

异常处理器会输出诊断信息并停止内核，它不是用户进程故障恢复机制。键盘 IRQ 路径目前会确认中断，但尚未实现键盘驱动。

## Build / 构建

The Makefile expects Clang, LLD (`ld.lld`), LLVM objcopy (`llvm-objcopy`) and Make. QEMU is used for runtime testing.

Makefile 预期使用 Clang、LLD（`ld.lld`）、LLVM objcopy（`llvm-objcopy`）和 Make。运行测试使用 QEMU。

```sh
make clean
make
make run
```

Optional host-side allocator test / 可选主机侧分配器测试：

```sh
make test-memory
```

See [BUILD_AND_TESTING_CN_EN.md](BUILD_AND_TESTING_CN_EN.md) for caveats and test interpretation.

测试注意事项和结果解释请阅读 [构建与测试说明](BUILD_AND_TESTING_CN_EN.md)。

## Repository layout / 仓库结构

```text
.
├── README.md
├── PROJECT_OVERVIEW_CN_EN.md
├── ARCHITECTURE_CN_EN.md
├── BUILD_AND_TESTING_CN_EN.md
├── Makefile
├── linker.ld
├── linker32.ld
├── linker64.ld
├── src/
│   ├── boot.S
│   ├── entry64.S
│   ├── gdt_load.S
│   ├── main.c
│   ├── idt.c
│   ├── excep.S
│   ├── exception.c
│   ├── pic.c
│   ├── irq.c / irq.h
│   ├── irq_entry.S
│   ├── timer.c
│   ├── memory.c / memory.h
│   ├── paging.c / paging.h
│   ├── task.c / task.h
│   ├── switch.S
│   └── serial.c / serial.h
├── tests/
│   ├── README.md
│   └── memory_host_test.c
└── tools/
    ├── apply_paging_rollback_fix.py
    └── fix_paging_rollback.py
```

## License and security / 许可证与安全

No license has been selected. All rights are reserved by default until a license is added. Do not assume redistribution or reuse is permitted. qmjyOS is experimental and must not be used to protect sensitive data.

项目尚未选择许可证。在正式添加许可证之前，默认保留所有权利。请勿自行假定允许再分发或复用。qmjyOS 属于实验性项目，不应用于保护敏感数据。
