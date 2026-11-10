# qmjyOS Project Overview / qmjyOS 项目完整介绍

## 1. Project identity / 项目定位

### 中文

qmjyOS 是一个以 x86_64 PC 体系结构为目标的实验性操作系统内核项目。它不是 Linux 发行版、桌面环境或 Android 应用，而是从引导入口开始，逐步建立处理器执行环境、内核运行基础设施和底层资源管理能力的独立内核原型。

项目的核心价值在于学习和验证操作系统最底层的机制：机器如何从引导协议进入内核、CPU 如何从 32 位保护模式进入 64 位长模式、内核如何设置段描述符和中断描述符、异常发生时如何保存现场、物理页如何分配、虚拟地址如何映射，以及多个 Ring 0 任务如何共享 CPU。

### English

qmjyOS is an experimental operating-system kernel targeting the x86_64 PC architecture. It is not a Linux distribution, desktop environment, or Android application. It is an independent kernel prototype that starts at the boot entry and progressively establishes the processor execution environment, kernel infrastructure, and low-level resource-management mechanisms.

The project's central purpose is to study and validate fundamental operating-system mechanisms: how a machine enters a kernel through a boot protocol, how the CPU transitions from 32-bit protected mode to 64-bit long mode, how the kernel configures segment and interrupt descriptors, how exception state is captured, how physical pages are allocated, how virtual addresses are mapped, and how multiple Ring 0 tasks share the CPU.

## 2. Goals / 项目目标

1. **Understand the boot chain.** Implement a Multiboot v1 entry, a 32-bit bootstrap, early paging structures, and a handoff to a 64-bit kernel image.
   **理解启动链路。** 实现 Multiboot v1 入口、32 位引导代码、早期分页结构，以及向 64 位内核映像移交控制权。

2. **Establish kernel execution state.** Set up a known stack, clear BSS, load the GDT, refresh CS, and configure a TSS descriptor.
   **建立内核运行状态。** 设置确定的栈、清零 BSS、加载 GDT、刷新 CS，并配置 TSS 描述符。

3. **Build interrupt and exception foundations.** Create IDT entries, assembly stubs, a common exception reporting path, PIC remapping and timer IRQ handling.
   **建立中断和异常基础。** 创建 IDT 表项、汇编入口桩、统一异常报告路径、PIC 重映射和定时器 IRQ 处理。

4. **Manage physical memory.** Parse Multiboot memory-map entries and allocate/free page-aligned physical frames while respecting reserved regions.
   **管理物理内存。** 解析 Multiboot 内存映射，在尊重保留区域的前提下分配和释放按页对齐的物理页框。

5. **Develop virtual-memory primitives.** Build and inspect 4 KiB page tables as an isolated, currently inactive manager.
   **开发虚拟内存基础操作。** 以独立、当前未激活的管理器形式构建和检查 4 KiB 页表。

6. **Preserve a Ring 0 multitasking baseline.** Demonstrate cooperative context switching between two kernel tasks.
   **保留 Ring 0 多任务基线。** 演示两个内核任务之间的协作式上下文切换。

7. **Prepare for privilege separation.** Keep GDT/TSS and page-permission work moving toward Ring 3, without claiming user mode is ready before the address-space design is validated.
   **为特权级隔离做准备。** 推进 GDT/TSS 和页权限工作，为 Ring 3 做准备，但在地址空间设计验证前不宣称用户态已经可用。

## 3. Design principles / 设计原则

- **Freestanding kernel:** the kernel is built without a hosted operating-system runtime or standard startup environment.
  **独立运行内核：** 内核不依赖宿主操作系统运行时或标准程序启动环境。

- **C plus assembly:** C implements most policy and data structures; assembly handles processor entry, interrupt frames, mode-sensitive operations, and context switching.
  **C 与汇编协作：** C 负责大部分策略和数据结构；汇编负责处理器入口、中断栈帧、对运行模式敏感的操作和上下文切换。

- **Explicit boundaries:** boot-time mappings, inactive page tables, cooperative scheduling, and future user mode are documented as separate capabilities.
  **边界明确：** 引导期映射、未激活页表、协作式调度和未来用户态被视为不同能力，分别说明。

- **Incremental validation:** build success, host tests, QEMU boot, interrupt activity, and privilege-transition tests are different levels of evidence.
  **逐步验证：** 编译成功、主机测试、QEMU 启动、中断活动和特权级切换测试属于不同层级的证据。

- **Keep recovery outside the release tree:** temporary snapshots and personal backups should remain outside the clean source tree.
  **恢复副本与发布源码分离：** 临时快照和个人备份应放在干净源码树之外。

## 4. Current feature inventory / 当前功能清单

| Area / 领域 | Current source state / 当前源码状态 | Boundary / 边界 |
|---|---|---|
| Boot / 引导 | Multiboot v1 32-bit bootstrap and 64-bit handoff / Multiboot v1 32 位引导和 64 位移交 | First 1 GiB identity map using 2 MiB pages / 前 1 GiB 使用 2 MiB 恒等映射 |
| Kernel entry / 内核入口 | 64-bit entry, stack, BSS clear / 64 位入口、栈和 BSS 清零 | Does not itself prove all boot configurations work / 不代表所有引导配置均已验证 |
| GDT/TSS | GDT reload helper and TSS descriptor / GDT 重载辅助函数和 TSS 描述符 | Ring 3 transition not enabled in current main / 当前主流程未启用 Ring 3 |
| Exceptions / 异常 | Stubs and serial panic output / 入口桩和串口 panic 输出 | Kernel halts; no process recovery / 内核停机，无进程级恢复 |
| IRQ/PIC/PIT | PIC remap, timer tick, IRQ0/1 path / PIC 重映射、tick 计数、IRQ0/1 路径 | Keyboard handler only acknowledges IRQ / 键盘处理目前仅确认中断 |
| Physical memory / 物理内存 | Multiboot mmap parser, bitmap allocator / Multiboot mmap 解析器、位图分配器 | Managed physical window limited to 1 GiB / 管理物理范围限制在 1 GiB |
| Paging / 分页 | Inactive 4 KiB page-table manager / 未激活的 4 KiB 页表管理器 | Does not activate CR3 or allocate data frames / 不激活 CR3，也不分配数据页框 |
| Scheduler / 调度器 | Two-task cooperative Ring 0 scheduler / 双任务 Ring 0 协作式调度器 | No timer-driven preemption / 无定时器驱动抢占 |
| Ring 3 / 用户态 | GDT/TSS and a transition helper exist / 存在 GDT/TSS 和切换辅助代码 | Disabled pending safe page permissions / 等待安全页权限，当前禁用 |

## 5. Repository layout / 仓库目录说明

- `src/boot.S`: Multiboot header, 32-bit bootstrap, initial page tables, long-mode enablement and 64-bit handoff.
  `src/boot.S`：Multiboot 头、32 位引导、初始页表、长模式开启和 64 位移交。

- `src/entry64.S`: 64-bit entry, argument preservation, stack setup, BSS clearing and call to `main`.
  `src/entry64.S`：64 位入口、参数保存、栈设置、BSS 清零和调用 `main`。

- `src/gdt_load.S`: loads the GDTR and performs a far return to reload CS.
  `src/gdt_load.S`：加载 GDTR，并通过远返回刷新 CS。

- `src/main.c`: initializes memory diagnostics, PIC, IDT, PIT, GDT/TSS, then starts the cooperative scheduler. The Ring 3 test is disabled.
  `src/main.c`：初始化内存诊断、PIC、IDT、PIT、GDT/TSS，然后启动协作式调度器；Ring 3 测试处于禁用状态。

- `src/idt.c`: constructs the 256-entry IDT and associates vectors with entry stubs.
  `src/idt.c`：构造 256 项 IDT，并将向量关联到入口桩。

- `src/excep.S` and `src/exception.c`: exception entry stubs and C-level panic reporting.
  `src/excep.S` 与 `src/exception.c`：异常汇编入口桩和 C 层 panic 报告。

- `src/pic.c`, `src/irq_entry.S`, `src/irq.c`: PIC remapping, IRQ entry/stack preservation, dispatch and EOI.
  `src/pic.c`、`src/irq_entry.S`、`src/irq.c`：PIC 重映射、IRQ 入口与寄存器保存、中断分发和 EOI。

- `src/timer.c`: programs PIT channel 0 and counts timer ticks.
  `src/timer.c`：配置 PIT 通道 0 并累计定时器 tick。

- `src/memory.c` and `src/memory.h`: parse Multiboot memory maps, track reserved ranges, allocate and free 4 KiB physical pages.
  `src/memory.c` 和 `src/memory.h`：解析 Multiboot 内存映射、跟踪保留区间、分配和释放 4 KiB 物理页。

- `src/paging.c` and `src/paging.h`: create a static pool of page-table pages and provide map/unmap/query operations.
  `src/paging.c` 和 `src/paging.h`：创建静态页表页池，并提供映射、解除映射和查询操作。

- `src/task.c`, `src/task.h`, `src/switch.S`: initialize two task stacks, yield cooperatively and switch saved CPU contexts.
  `src/task.c`、`src/task.h`、`src/switch.S`：初始化两个任务栈、协作式让出 CPU，并切换保存的 CPU 上下文。

- `src/serial.c` and `src/serial.h`: serial output used for early boot and panic diagnostics.
  `src/serial.c` 和 `src/serial.h`：用于早期启动和 panic 诊断的串口输出。

- `tests/`: host-side allocator test harness and test instructions.
  `tests/`：主机侧分配器测试程序及测试说明。

- `tools/`: Python source-maintenance/repair scripts. These are development utilities, not runtime kernel components.
  `tools/`：Python 源码维护/修复脚本，属于开发工具，不是内核运行组件。

## 6. Runtime flow / 运行流程概览

The processor begins at the Multiboot 32-bit entry. The bootstrap copies the embedded 64-bit binary to its linked physical address, constructs initial identity mappings, enables long mode and paging, and transfers control to `_start64`. The 64-bit entry establishes its stack, clears BSS, and calls `main`. The C main path initializes memory subsystems and interrupt infrastructure, configures GDT/TSS, enables interrupts, and enters the first cooperative task. Tasks yield explicitly through `task_yield`; the timer interrupt only increments the tick counter.

处理器从 Multiboot 32 位入口开始。引导代码把嵌入的 64 位二进制复制到链接指定的物理地址，建立初始恒等映射，开启长模式和分页，然后转交 `_start64`。64 位入口建立栈、清零 BSS 并调用 `main`。C 主流程初始化内存子系统和中断基础设施，配置 GDT/TSS，开启中断并进入第一个协作式任务。任务通过 `task_yield` 显式让出 CPU；定时器中断目前只增加 tick 计数。

## 7. Roadmap / 后续路线

1. Keep the boot chain reproducible and document the exact QEMU command/output.
   保持启动链路可重复，并记录准确的 QEMU 命令和输出。
2. Strengthen exception/IRQ frame definitions and test all installed vectors.
   加强异常/IRQ 栈帧定义，并测试所有已安装向量。
3. Connect the physical allocator to page-table page allocation with explicit ownership and rollback.
   将物理分配器与页表页分配连接起来，明确所有权和回滚规则。
4. Design a kernel virtual-memory layout and a safe address-space activation sequence.
   设计内核虚拟内存布局和安全的地址空间激活流程。
5. Enable NX only after CPU feature detection and EFER configuration are implemented and tested.
   只有在完成并测试 CPU 特性检测和 EFER 配置后，才启用 NX。
6. Split or construct user mappings with U/S permissions at every paging level; keep kernel mappings supervisor-only.
   在页表每一级正确设置 U/S 权限，构建用户映射，同时保持内核映射为 supervisor-only。
7. Validate Ring 3 entry, TSS stack switching, fault containment and a controlled syscall ABI.
   验证 Ring 3 入口、TSS 栈切换、故障隔离和受控系统调用 ABI。
8. Preserve cooperative scheduling as a known baseline before introducing timer-driven preemption.
   在引入定时器抢占之前，保留协作式调度作为已知基线。
9. Add automated QEMU regression tests and a documented license.
   增加自动化 QEMU 回归测试，并明确项目许可证。

## 8. Non-goals and disclaimers / 非目标与声明

qmjyOS is not currently a general-purpose OS, secure sandbox, hypervisor, or production kernel. It does not yet provide a complete userspace, filesystem, process model, device-driver ecosystem, networking stack, or robust recovery from kernel faults.

qmjyOS 当前还不是通用操作系统、安全沙箱、虚拟机监控程序或生产级内核。它尚未提供完整用户空间、文件系统、进程模型、设备驱动生态、网络协议栈或可靠的内核故障恢复机制。
