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

5. **Develop virtual-memory primitives.** Build, inspect and activate 4 KiB page tables; the Phase 3 runtime uses a private address-space root and checks user/kernel mapping permissions.
   **开发虚拟内存基础操作。** 构建、检查并激活 4 KiB 页表；Phase 3 运行时使用独立地址空间根并检查用户/内核映射权限。

6. **Preserve a Ring 0 multitasking baseline.** Demonstrate cooperative context switching between two kernel tasks.
   **保留 Ring 0 多任务基线。** 演示两个内核任务之间的协作式上下文切换。

7. **Privilege separation foundation.** GDT/TSS, user-page permission checks, private address-space activation and CPL3 entry are integrated for the test runtime; complete process isolation remains future work.
   **特权级隔离基础。** GDT/TSS、用户页权限检查、独立地址空间激活及 CPL3 入口已接入测试运行时；完整进程隔离仍是后续工作。

## 3. Design principles / 设计原则

- **Freestanding kernel:** the kernel is built without a hosted operating-system runtime or standard startup environment.
  **独立运行内核：** 内核不依赖宿主操作系统运行时或标准程序启动环境。

- **C plus assembly:** C implements most policy and data structures; assembly handles processor entry, interrupt frames, mode-sensitive operations, and context switching.
  **C 与汇编协作：** C 负责大部分策略和数据结构；汇编负责处理器入口、中断栈帧、对运行模式敏感的操作和上下文切换。

- **Explicit boundaries:** boot-time mappings, inactive page-table construction, the active Phase 3 test user-mode runtime, and cooperative scheduling are documented as separate capabilities.
  **边界明确：** 引导期映射、未激活页表构建、已接入的 Phase 3 测试用户态运行时和协作式调度被视为不同能力，分别说明。

- **Incremental validation:** build success, host tests, QEMU boot, interrupt activity, and privilege-transition tests are different levels of evidence.
  **逐步验证：** 编译成功、主机测试、QEMU 启动、中断活动和特权级切换测试属于不同层级的证据。

- **Keep recovery outside the release tree:** temporary snapshots and personal backups should remain outside the clean source tree.
  **恢复副本与发布源码分离：** 临时快照和个人备份应放在干净源码树之外。

## 4. Current feature inventory / 当前功能清单

| Area / 领域 | Current source state / 当前源码状态 | Boundary / 边界 |
|---|---|---|
| Boot / 引导 | Multiboot v1 32-bit bootstrap and 64-bit handoff / Multiboot v1 32 位引导和 64 位移交 | First 1 GiB identity map using 2 MiB pages / 前 1 GiB 使用 2 MiB 恒等映射 |
| Kernel entry / 内核入口 | 64-bit entry, stack, BSS clear / 64 位入口、栈和 BSS 清零 | Does not itself prove all boot configurations work / 不代表所有引导配置均已验证 |
| GDT/TSS | GDT reload helper, TSS descriptor and RSP0 kernel stack / GDT 重载辅助函数、TSS 描述符和 RSP0 内核栈 | Basic Ring 3 transition is exercised by the runtime test / 运行测试已验证基础 Ring 3 切换 |
| Exceptions / 异常 | Stubs and serial panic output / 入口桩和串口 panic 输出 | Kernel halts; no process recovery / 内核停机，无进程级恢复 |
| IRQ/PIC/PIT | PIC remap, timer tick, IRQ0/1 path / PIC 重映射、tick 计数、IRQ0/1 路径 | Keyboard handler only acknowledges IRQ / 键盘处理目前仅确认中断 |
| Physical memory / 物理内存 | Multiboot mmap parser, bitmap allocator / Multiboot mmap 解析器、位图分配器 | Managed physical window limited to 1 GiB / 管理物理范围限制在 1 GiB |
| Paging / 分页 | 4 KiB page-table manager with CR3 activation / 支持 CR3 激活的 4 KiB 页表管理器 | Runtime test checks user RX, user RW/NX and supervisor-only kernel mapping / 运行测试检查用户 RX、用户 RW/NX 和仅内核可访问的映射 |
| Scheduler / 调度器 | Two-task cooperative Ring 0 scheduler / 双任务 Ring 0 协作式调度器 | No timer-driven preemption / 无定时器驱动抢占 |
| Ring 3 / 用户态 | Basic CPL3 entry, isolated user code/stack mappings and int 0x80 test syscall / 基础 CPL3 入口、隔离的用户代码与栈映射及 int 0x80 测试系统调用 | Basic transition test passes; this is not a complete process model or security audit / 基础切换测试通过，但这不代表完整进程模型或安全审计 |

## 5. Repository layout / 仓库目录说明

- `boot/bios/boot.S`: Multiboot header, 32-bit bootstrap, initial page tables, long-mode enablement and 64-bit handoff.
  `boot/bios/boot.S`：Multiboot 头、32 位引导、初始页表、长模式开启和 64 位移交。

- `arch/x86_64/boot/entry64.S`: 64-bit entry, argument preservation, stack setup, BSS clearing and call to `main`.
  `arch/x86_64/boot/entry64.S`：64 位入口、参数保存、栈设置、BSS 清零和调用 `main`。

- `arch/x86_64/gdt/gdt_load.S`: loads the GDTR and performs a far return to reload CS.
  `arch/x86_64/gdt/gdt_load.S`：加载 GDTR，并通过远返回刷新 CS。

- `kernel/core/main.c`: initializes physical memory/paging, interrupt infrastructure and GDT/TSS; runs the ELF process-runtime Ring 3 test including echo/exit and reclamation; then starts the cooperative Ring 0 scheduler.
  `kernel/core/main.c`：初始化物理内存/分页、 中断基础设施及 GDT/TSS；执行 ELF 进程运行时 Ring 3 测试（echo/exit 和回收）；随后启动 Ring 0 协作式调度器。

- `arch/x86_64/idt/idt.c`: constructs the 256-entry IDT and associates vectors with entry stubs.
  `arch/x86_64/idt/idt.c`：构造 256 项 IDT，并将向量关联到入口桩。

- `arch/x86_64/interrupts/excep.S` and `arch/x86_64/interrupts/exception.c`: exception entry stubs and C-level panic reporting.
  `arch/x86_64/interrupts/excep.S` 与 `arch/x86_64/interrupts/exception.c`：异常汇编入口桩和 C 层 panic 报告。

- `arch/x86_64/interrupts/pic.c`, `arch/x86_64/interrupts/irq_entry.S`, `arch/x86_64/interrupts/irq.c`: PIC remapping, IRQ entry/stack preservation, dispatch and EOI.
  `arch/x86_64/interrupts/pic.c`、`arch/x86_64/interrupts/irq_entry.S`、`arch/x86_64/interrupts/irq.c`：PIC 重映射、IRQ 入口与寄存器保存、中断分发和 EOI。

- `arch/x86_64/interrupts/timer.c`: programs PIT channel 0 and counts timer ticks.
  `arch/x86_64/interrupts/timer.c`：配置 PIT 通道 0 并累计定时器 tick。

- `mm/physical/memory.c` and `mm/physical/memory.h`: parse Multiboot memory maps, track reserved ranges, allocate and free 4 KiB physical pages.
  `mm/physical/memory.c` 和 `mm/physical/memory.h`：解析 Multiboot 内存映射、跟踪保留区间、分配和释放 4 KiB 物理页。

- `mm/paging/paging.c` and `mm/paging/paging.h`: create a static pool of page-table pages and provide map/unmap/query operations.
  `mm/paging/paging.c` 和 `mm/paging/paging.h`：创建静态页表页池，并提供映射、解除映射和查询操作。

- `sched/core/task.c`, `sched/core/task.h`, `arch/x86_64/cpu/switch.S`: initialize two task stacks, yield cooperatively and switch saved CPU contexts.
  `sched/core/task.c`、`sched/core/task.h`、`arch/x86_64/cpu/switch.S`：初始化两个任务栈、协作式让出 CPU，并切换保存的 CPU 上下文。

- `drivers/serial/serial.c` and `drivers/serial/serial.h`: serial output used for early boot and panic diagnostics.
  `drivers/serial/serial.c` 和 `drivers/serial/serial.h`：用于早期启动和 panic 诊断的串口输出。

- `tests/`: host-side allocator test harness and test instructions.
  `tests/`：主机侧分配器测试程序及测试说明。

- `tools/`: Python source-maintenance/repair scripts. These are development utilities, not runtime kernel components.
  `tools/`：Python 源码维护/修复脚本，属于开发工具，不是内核运行组件。

## 6. Runtime flow / 运行流程概览

The BIOS processor path begins at the Multiboot 32-bit entry. The bootstrap copies the embedded 64-bit binary, establishes initial mappings, enables long mode and paging, and transfers control to `_start64`. The 64-bit entry sets up the stack, clears BSS and calls `main`. The main path initializes physical memory/paging, interrupt infrastructure and GDT/TSS, registers test syscall handlers, then runs the ELF process runtime: private page-table root, USER/RW/NX stack, CR3 activation, CPL3 echo/exit probe and page reclamation. After the test exits, the kernel starts the cooperative Ring 0 scheduler. Tasks yield explicitly; timer interrupts do not preemptively switch task contexts.

BIOS 路径从 Multiboot 32 位入口开始。引导代码复制嵌入的 64 位映像，建立初始映射，开启长模式和分页后进入 `_start64`。64 位入口设置栈、清零 BSS 并调用 `main`。主流程初始化物理内存/分页、中断和 GDT/TSS，注册测试 syscall，再运行 ELF 进程运行时：独立页表根、USER/RW/NX 用户栈、CR3 激活、CPL3 echo/exit 测试及页面回收。测试退出后启动 Ring 0 协作式调度器。任务显式让出 CPU；定时器不会抢占式切换任务。

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
7. Extend Ring 3 validation to cover TSS stack switching, fault containment, syscall ABI rules and invalid user-memory accesses.
   进一步验证 Ring 3 的 TSS 栈切换、故障隔离、系统调用 ABI 规则和非法用户内存访问。
8. Preserve cooperative scheduling as a known baseline before introducing timer-driven preemption.
   在引入定时器抢占之前，保留协作式调度作为已知基线。
9. Add automated QEMU regression tests and a documented license.
   增加自动化 QEMU 回归测试，并明确项目许可证。

## 8. Non-goals and disclaimers / 非目标与声明

qmjyOS is not currently a general-purpose OS, secure sandbox, hypervisor, or production kernel. It does not yet provide a complete userspace, filesystem, process model, device-driver ecosystem, networking stack, or robust recovery from kernel faults.

qmjyOS 当前还不是通用操作系统、安全沙箱、虚拟机监控程序或生产级内核。它尚未提供完整用户空间、文件系统、进程模型、设备驱动生态、网络协议栈或可靠的内核故障恢复机制。

<!-- QMjyOS-DOCS-CI-14 -->

## Historical Development Status — 2026-11-16

### 中文：当前工程状态

当前已验证的开发基线：

- x86_64 BIOS 构建路径。
- 基础内核启动和串口输出。
- GDT、TSS、IDT 等 CPU 初始化基础。
- 物理内存管理、分页和堆管理基础组件。
- RAMFS/VFS 基础组件。
- 基础 Ring 3 特权级转换测试。
- Ring 0 协作式双任务调度测试。
- QBuild 1.0.0 构建/测试入口；CI 覆盖 x86_64 BIOS 和初步 UEFI。

功能边界：

1. 当前 Ring 0 调度器是协作式调度，不是抢占式调度。
2. 当前进程模块的 metadata registry 不等于完整进程管理器。
3. 当前 Ring 3 测试不等于完整用户地址空间隔离。
4. ELF64 加载相关代码不等于完整、独立的进程地址空间。
5. BIOS 启动链仍属于开发阶段 bootstrap；UEFI 已有初步 CI 测试实现，但尚未达到发布级。
6. QBuild 已具备模块和构建管理能力，但底层仍依赖 Makefile。

### CI 状态

本地已通过源码语法、QBuild 构建、ELF 布局、
QBuild 测试、ASan/UBSan 主机测试和 git diff 检查。

Termux 不支持 LeakSanitizer，本地没有完成内存泄漏检测。

GitHub Actions Run #14：
https://github.com/qmjy592/qmjyOS/actions/runs/43826588727

最终 CI 状态需要以该运行记录为准。

### English: Current Engineering Status

The current verified development baseline includes:

- x86_64 BIOS build support.
- Basic kernel boot and serial output.
- GDT, TSS, and IDT initialization foundations.
- Physical memory, paging, and heap foundations.
- Basic RAMFS/VFS components.
- A basic Ring 3 privilege-transition test.
- A cooperative Ring 0 two-task scheduler test.
- QBuild 1.0.0 build/test frontend; CI covers x86_64 BIOS and initial UEFI.

Important limitations:

1. The current scheduler is cooperative, not preemptive.
2. The process metadata registry is not a complete process manager.
3. The Ring 3 test does not prove complete user-space isolation.
4. ELF64 loading support does not prove independent process address spaces.
5. The BIOS boot path remains a development bootstrap; UEFI has an initial CI-tested implementation but is not release-ready.
6. QBuild still relies on Makefile-based underlying build rules.

Local checks passed for source syntax, QBuild build, ELF layout,
QBuild tests, ASan/UBSan host tests, and git diff validation.

LeakSanitizer is unsupported in the current Termux environment.
The final status of CI Run #14 must be checked separately.

## Development Status Update — 2026-11-21 (Phase 3 runtime)

### 中文

本次阶段进展以本节为准，覆盖旧的 Phase 2/3 “尚未连接独立地址空间”描述：

- ELF64 → process runtime 的集成路径已建立。
- 用户地址空间使用独立页表根；内核映射仍需按当前映射策略保持 Supervisor 权限。
- 用户栈页检查为 USER/RW/NX。
- 运行路径激活私有 CR3，并进入 CPL3。
- 测试系统调用包含 echo 往返和 exit 路径；退出后释放运行时页。
- 用户在 Termux 上报告实际 QEMU BIOS 串口标志通过。原始串口文件未包含在本次审查压缩包中。
- 新增主机回归测试：ELF process runtime、scheduler/address-space CR3、x86_64 syscall trap-frame ABI。

仍未宣称完成：多用户进程并发调度、生产级 syscall ABI、完整 fault-safe user-copy、用户异常全面恢复、抢占式调度、SMP、安全审计。

### English

This status supersedes earlier Phase 2/3 statements that the private address-space runtime was not yet connected. Detailed acceptance evidence and limitations are recorded in [Phase 3 Runtime Validation](docs/development/PHASE3_RUNTIME_VALIDATION_2026-11-21.md).

- ELF64-to-process runtime integration is present.
- The user process receives a private page-table root; shared kernel mappings remain supervisor-only by design.
- The user stack is checked as USER/RW/NX.
- The private CR3 is activated before CPL3 entry.
- The smoke-test syscalls cover echo/return and exit; runtime pages are reclaimed after exit.
- The user reported a QEMU BIOS run passing the runtime serial markers. The raw serial log was not included in the review archive.
- Host regression targets cover process runtime, scheduler/address-space CR3 behavior, and the x86_64 syscall trap-frame ABI.

This does not establish concurrent multi-process scheduling, a production syscall ABI, fault-safe user copies, complete user exception recovery, preemption, SMP, or a security audit.

CI builds x86_64 BIOS and UEFI paths. AArch64 remains unsupported.


## Current Development Status — 2026-11-21


## Development and CI Status — 2026-11-21

Current development phase: **Phase 3 — Runtime Integration and Hardening**.

Phase 1 system-foundation work has been accepted. Phase 2 kernel-infrastructure foundations are in place. Phase 3 has an experimental ELF64 process-runtime path, but Phase 3 is not complete and qmjyOS 0.1.0 is not yet release-ready.

### Implemented foundations and limited evidence

- x86_64 BIOS development bootstrap and initial x86_64 UEFI/OVMF path.
- GDT/TSS/IDT, exception-entry, PIC/PIT/IRQ foundations.
- Physical-page allocator, 4 KiB paging manager, and limited kernel-heap foundations.
- ELF64 loader integrated with an experimental process runtime.
- Private page-table root, USER/RW/NX user-stack validation, CR3 activation, and CPL3 entry.
- Test syscall echo/return and exit path, followed by runtime-page reclamation.
- Ring 0 two-task cooperative scheduling; this is not preemptive scheduling.
- VFS/RAMFS foundations, QFS host tests, and QBuild 1.0.0 build/test entry points.

Host tests do not prove hardware TLB behavior or complete isolation.

### Remaining work

- Fault-safe user-copy and comprehensive user-exception recovery.
- Concurrent independent user processes, per-task kernel stacks, and complete process/thread lifecycle.
- Stable, documented production syscall ABI and authorization rules.
- Preemptive scheduling and SMP.
- Complete self-developed QBoot BIOS manager; UEFI hardening and compatibility work.
- Persistent storage, complete driver ecosystem, desktop, installer, recovery, and full bilingual product experience.
- Full security audit and broader hardware/firmware validation.

### GitHub Actions snapshot

- Commit: `927eb3665cb2a9b83670749ed84e2555709ba255`
- Workflow: [Run #19](https://github.com/qmjy592/qmjyOS/actions/runs/44794506369)
- Code and QBuild validation: **PASS**.
- x86_64 BIOS build and QEMU boot test: **PASS**.
- x86_64 UEFI/OVMF job: **RUNNING at the time checked**.

This is a dated snapshot, not a live status. Do not describe the complete workflow as green until the UEFI job completes.

### 中文：当前阶段与 CI 状态

当前重点为**第三阶段：运行时集成与加固**。第一阶段系统骨架已验收，第二阶段内核基础设施已形成基础，第三阶段已经接通实验性的 ELF64 进程运行时路径，但尚未完成。

已建立基础或有限验证的能力包括：x86_64 BIOS 开发引导、初步 UEFI/OVMF 路径、GDT/TSS/IDT、异常入口、PIC/PIT/IRQ、物理页分配器、4 KiB 分页、有限内核堆、ELF64 加载器、实验性进程运行时、私有页表根、用户栈权限检查、CR3 激活、CPL3 入口、测试 syscall、运行时页回收、Ring 0 协作式调度、VFS/RAMFS、QFS 测试和 QBuild 1.0.0。

仍未完成的工作包括 fault-safe user-copy、全面用户异常恢复、多独立进程并发调度、每任务内核栈、完整进程/线程生命周期、生产级 syscall ABI、抢占式调度、SMP、完整 QBoot BIOS 管理器、UEFI 加固、持久化存储、完整驱动生态、桌面、安装器、恢复环境、完整双语体验和全面安全审计。

本次查询时，提交 `927eb3665cb2a9b83670749ed84e2555709ba255` 对应的 [GitHub Actions Run #19](https://github.com/qmjy592/qmjyOS/actions/runs/44794506369) 中，Code and QBuild validation 已通过，x86_64 BIOS build and QEMU boot test 已通过，UEFI/OVMF Job 仍在运行。因此不能将整个 workflow 标记为全绿。
