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
- A 4 KiB page-table manager with map/unmap/query operations; the Ring 3 runtime test activates a user address space through CR3.
- Two-task cooperative Ring 0 scheduler and assembly context switch.
- Host-side tests for the physical memory allocator.

- Multiboot v1 32 位引导程序，以及进入 64 位内核映像的切换流程。
- 使用 2 MiB 页建立前 1 GiB 的早期恒等映射。
- 64 位入口、栈设置、BSS 清零和 C 语言入口。
- GDT 重新加载、TSS 描述符构造和特权级切换所需的内核栈设置。
- IDT 基础设施、异常入口桩和串口 panic 诊断。
- 8259 PIC 重映射、PIT 编程、IRQ0/IRQ1 入口与分发。
- Multiboot 内存映射解析器和早期物理页分配器。
- 4 KiB 页表管理器提供映射、解除映射和查询操作；Ring 3 运行测试会通过 CR3 激活用户地址空间。
- 双任务 Ring 0 协作式调度器和汇编上下文切换。
- 物理内存分配器的主机侧测试。

## Important implementation boundaries / 重要实现边界

The bootstrap currently maps the first 1 GiB with 2 MiB pages. This is not yet a complete per-process address-space design. The page-table manager maintains a separate static table pool and does not activate its root by writing CR3. Its self-test therefore tests page-table data structures without switching the live address space.

引导阶段目前使用 2 MiB 页映射前 1 GiB。这还不是完整的进程地址空间设计。分页管理器维护独立的静态页表池，不会通过写入 CR3 激活其根页表。因此，它的自测试验证的是页表数据结构，而不是实际切换运行中的地址空间。

The current kernel main path initializes memory and interrupt infrastructure, configures GDT/TSS, runs the basic Ring 3 address-space and syscall test, and then starts the cooperative Ring 0 scheduler. The PIT interrupt increments a tick counter; it does not preempt tasks or switch contexts.

当前内核主流程会初始化内存和中断基础设施，配置 GDT/TSS，执行基础 Ring 3 地址空间及系统调用测试，然后启动 Ring 0 协作式调度器。PIT 中断只增加 tick 计数，不会抢占任务或切换上下文。

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
├── Makefile
├── linker32.ld
├── linker64.ld
├── ARCHITECTURE_CN_EN.md
├── BUILD_AND_TESTING_CN_EN.md
├── PROJECT_OVERVIEW_CN_EN.md
├── RING3_VALIDATION_2026-11-10.md
├── arch/
├── assets/
├── boot/
├── config/
├── docs/
│   └── history/
├── drivers/
├── efi/
├── fs/
├── init/
├── ipc/
├── kernel/
├── lib/
├── mm/
├── network/
├── runtime/
├── sched/
├── scripts/
├── security/
├── tests/
├── tools/
└── user/
```

## License and security / 许可证与安全

No license has been selected. All rights are reserved by default until a license is added. Do not assume redistribution or reuse is permitted. qmjyOS is experimental and must not be used to protect sensitive data.

项目尚未选择许可证。在正式添加许可证之前，默认保留所有权利。请勿自行假定允许再分发或复用。qmjyOS 属于实验性项目，不应用于保护敏感数据。

<!-- QMjyOS-RING3-STATUS-START -->
## 最新开发进度（2026-11-10）

### Ring 3 基础特权级切换：已通过 QEMU 验收

qmjyOS 已经在 QEMU 中完成真实的 Ring 0 → Ring 3 → Ring 0 特权级往返测试，并在返回内核态后继续运行原有 Ring 0 协作式双任务调度器。

目前已经实现：

- GDT 与 TSS 初始化。
- 配置 TSS.RSP0 内核栈。
- 使用 `iretq` 从 Ring 0 进入 CPL3。
- 建立独立的用户态代码页和用户栈映射。
- 用户代码页设置为 RX、U/S=1。
- 用户栈设置为 RW/NX、U/S=1。
- 内核身份映射保持 Supervisor-only。
- 用户态通过 `int 0x80` 进入 Ring 0 系统调用处理程序。
- 系统调用完成后成功返回 Ring 0。
- Ring 3 测试结束后，原有 Ring 0 Task A / Task B 调度器继续运行。

### 当前测试结果

| 测试项目 | 结果 |
|---|---|
| `make clean && make` | 通过 |
| `make test-memory` | 通过 |
| `make test-paging` | 通过（主机端逻辑测试） |
| `make test-boot` | 通过 |
| QEMU 内核启动 | 通过 |
| Ring 0 → Ring 3 → Ring 0 | 实际运行通过 |
| `int 0x80` 系统调用 | 实际运行通过 |
| Ring 0 Task A / Task B 调度 | 实际运行通过 |

### 尚未完成的安全验证

基础特权级切换通过，不代表所有用户态安全隔离测试已经完成。

下一阶段需要完成：

1. Ring 3 写入只读用户代码页时触发 Page Fault。
2. Ring 3 访问 Supervisor-only 内核页面时触发 Page Fault。
3. Ring 3 执行 NX 用户栈时触发 Page Fault。
4. 验证 Page Fault 的 CR2、错误码和异常处理路径。
5. 支持多个独立用户地址空间。
6. 完善系统调用 ABI。
7. 后续推进抢占式调度。

### 下一阶段开发方向

优先建立安全、可恢复的异常测试框架，再逐项验证用户代码页只读保护、内核页面访问保护和 NX 执行保护。

现有 Ring 0 协作式调度器保留在 `sched/core/task.c`、`sched/core/task.h` 和 `arch/x86_64/cpu/switch.S`。Ring 3 基础特权级往返测试已接入主流程，不以删除原有调度器为代价。

详细验收记录见 [`RING3_VALIDATION_2026-11-10.md`](RING3_VALIDATION_2026-11-10.md)。
<!-- QMjyOS-RING3-STATUS-END -->
