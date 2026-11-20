# qmjyOS — 自研实验性操作系统 / Experimental Operating System

qmjyOS is a self-developed experimental operating-system project, primarily written in freestanding C and x86 assembly. The current development baseline targets x86_64, with BIOS and an initial UEFI boot path under CI validation. The project is building a modular kernel foundation, architecture boundaries, public interfaces, memory management, interrupt handling, filesystems, scheduling, and its own boot infrastructure named QBoot.

qmjyOS 是一个自研的实验性操作系统项目，主要使用 freestanding C 和 x86 汇编开发。当前开发基线为 x86_64；BIOS 开发引导链和初步 UEFI 引导路径正在 CI 验证。项目逐步建设模块化内核基础设施、架构边界、公共接口、内存管理、中断处理、文件系统、任务调度以及自研启动体系 QBoot。

> **Project status / 项目状态:** Phase 1 — System Foundation completed and uploaded on 2026-11-11. The project remains experimental, is not production-ready, and has not undergone a complete security audit.
>
> **当前状态：** 第一阶段“系统骨架建设”已于 2026-11-11 完成验收并上传仓库。项目仍处于实验开发阶段，不适合生产环境，尚未完成全面安全审计。

> **Phase 2/3 status (2026-11-21):** the kernel now has a tested host-side ELF-to-process runtime path with private page-table root setup, user stack permission validation, CR3 activation, CPL3 entry, syscall echo/exit probe, and process exit/page reclamation. The user reported an actual QEMU BIOS run passing the new Ring 3 process-runtime markers. The raw serial log was not included in the review snapshot. This is experimental—not a production process manager or security boundary.


## Documentation / 文档导航

### Project and development / 项目与开发
- [Phase 1 Completion and Acceptance / 第一阶段完成与验收](docs/development/PHASE1_COMPLETION.md)
- [Phase 2/3 Integration Status / 第二、三阶段集成状态](docs/architecture/PHASE2_PHASE3_INTEGRATION.md)
- [Phase 3 Runtime Validation / 第三阶段运行时验收](docs/development/PHASE3_RUNTIME_VALIDATION_2026-11-21.md)
- [QBuild Documentation / QBuild 构建工具说明](tools/qbuild/README.md)
- [Build and Test Guide / 构建与测试说明](tests/README.md)
- [QTest Development Plan / QTest 开发计划](tests/qtest/README.md)

### Architecture and interfaces / 架构与接口
- [System Layout / 系统目录布局](docs/architecture/SYSTEM_LAYOUT.md)
- [Interface Contracts / 公共接口契约](docs/architecture/INTERFACE_CONTRACTS.md)
- [Module Status Model / 模块状态模型](docs/architecture/MODULE_STATUS_MODEL.md)
- [Module Dependencies / 模块依赖说明](docs/architecture/MODULE_DEPENDENCIES.md)
- [Historical System Architecture / 历史系统架构参考](docs/architecture/SYSTEM_ARCHITECTURE.md)

### Boot and language editions / 启动体系与语言版本
- [QBoot Directory and Status / QBoot 目录与实现状态](boot/README.md)
- [QBoot Handoff Protocol / QBoot 内核交接协议](boot/protocols/README.md)
- [Language Edition Policy / 中英文版本政策](docs/internationalization/LANGUAGE_POLICY.md)
- [Ring 3 QEMU Milestone Record / Ring 3 QEMU 里程碑记录](RING3_VALIDATION_2026-11-10.md)
- [Phase 3 Runtime Validation / 第三阶段运行时验收](docs/development/PHASE3_RUNTIME_VALIDATION_2026-11-21.md)

## Phase 1 — System Foundation / 第一阶段：系统骨架

Phase 1 establishes a coherent repository structure and explicit contracts between subsystems. It is a foundation and integration milestone, not a claim that the entire operating system or every listed module is implemented.

第一阶段的目标是建立一致的仓库结构，并明确各子系统之间的接口契约。这是系统基础与构建集成阶段，不代表整个操作系统已经完成，也不代表所有模块都已实现。

### Delivered in Phase 1 / 本阶段完成内容

- Documented top-level directory responsibilities and architecture-specific boundaries.
- Added public kernel interface headers under `include/qmjy/` for architecture, boot information, devices, block I/O, filesystems, IPC, kernel services, logging, memory, modules, panic handling, syscalls, tasks, and tests.
- Defined common interface conventions and the versioned QBoot-to-kernel handoff contract.
- Established module configuration, dependency, and implementation-status inventories.
- Integrated QBuild v0.5.0 with module validation, dependency planning, build integration, and existing test targets.
- Documented the x86_64, AArch64, BIOS, and UEFI directory boundaries and their actual implementation status.
- Documented separate Simplified Chinese and English international editions.
- Added or clarified host-side tests, public-header checks, boot-image static checks, and QTest planning documentation.

- 完成顶层目录职责和架构相关代码边界说明。
- 在 `include/qmjy/` 建立架构、启动信息、设备、块设备、文件系统、IPC、内核服务、日志、内存、模块、Panic、系统调用、任务和测试等公共接口头文件。
- 定义公共接口约定，以及带版本的 QBoot 到内核交接协议。
- 建立模块配置、依赖关系和实现状态清单。
- 将 QBuild v0.5.0 接入模块校验、依赖规划、现有构建流程和测试目标。
- 明确 x86_64、AArch64、BIOS、UEFI 目录边界及其真实实现状态。
- 明确简体中文版本与全英文国际版本的独立语言政策。
- 补充和整理主机侧测试、公共头文件检查、启动映像静态检查及 QTest 规划文档。

### Phase 1 final validation / 第一阶段最终验收

On 2026-11-11, the following local validation completed successfully:

- Shell syntax check for `tools/qbuild/qbuild`.
- Python syntax check for `tools/qbuild/module_plan.py`.
- QBuild x86_64 + BIOS configuration, dependency, status, and source-mapping checks.
- Full build: `make -B -j4 all`.
- QBuild test suite: `./tools/qbuild/qbuild test --arch x86_64 --boot bios --jobs 4`.
- Physical memory allocator host tests.
- Paging manager host self-test.
- VFS and RAMFS host tests.
- Public interface header compilation test.
- BIOS boot-image static verification.
- Documentation entry-point checks, build-artifact ignore checks, and `git diff --check`.

第一阶段于 2026-11-11 完成最终本地验收：Shell/Python 语法、QBuild 配置与模块映射、完整内核构建、物理内存测试、分页主机侧自测试、VFS、RAMFS、公共接口头文件编译、BIOS 启动映像静态检查、文档入口、构建产物忽略规则和 Git 差异检查均通过。

### Current QBuild module inventory / 当前模块统计

| Item | Acceptance value |
|---|---:|
| Total modules | 58 |
| Modules with source mappings | 22 |
| Modules in the current build plan | 17 |
| Status: implemented | 1 |
| Status: experimental | 20 |
| Status: planned | 37 |

These are separate metadata counts reported by QBuild 1.0.0 on 2026-11-21: source mappings, current build-plan membership, and implementation status are not interchangeable. A header, directory, dependency entry, or successful compile does not by itself make a subsystem fully implemented.

以上是 QBuild 1.0.0 于 2026-11-21 输出的不同维度统计：源码映射数量、当前构建计划模块数量和实现状态数量不能混为一谈。仅有头文件、目录、依赖条目或编译成功，并不意味着子系统已经完整实现。

## Current implementation snapshot / 当前实现概况

The following describes the current source tree and documented test evidence. It distinguishes implemented code from limited tests and planned work.

以下内容描述当前源码树和已有测试证据，并区分已存在的代码、有限范围的测试以及仍在规划的工作。

### Boot and architecture / 启动与架构

- CI build/runtime coverage currently targets **x86_64 + BIOS and x86_64 + UEFI** (UEFI remains an initial implementation).
- The BIOS path contains a Multiboot-based 32-bit development bootstrap and a transition into the 64-bit kernel image.
- Early boot establishes an identity mapping for the first 1 GiB using 2 MiB pages.
- The current bootstrap is not the complete QBoot BIOS boot manager.
- x86_64 UEFI has an initial build/runtime CI path; AArch64 targets remain unsupported and are rejected by QBuild.
- QBoot is the project-owned boot-manager direction; GRUB or another third-party boot manager is not the intended final boot manager.

- 当前 CI 构建/运行覆盖目标包括 **x86_64 + BIOS 与 x86_64 + UEFI**（UEFI 仍是初步实现）。
- BIOS 路径包含基于 Multiboot 的 32 位开发引导程序，并切换到 64 位内核映像。
- 早期启动使用 2 MiB 页建立前 1 GiB 的恒等映射。
- 当前 bootstrap 还不是完整的 QBoot BIOS 启动管理器。
- x86_64 UEFI 已具备初步构建/运行 CI 路径；AArch64 目标仍未支持，QBuild 会明确拒绝。
- QBoot 是项目计划自研的启动管理器，不以 GRUB 或其他第三方启动管理器作为最终方案。

### CPU, interrupts, and scheduling / CPU、中断与调度

- x86_64 entry code, GDT reload, TSS setup, IDT infrastructure, exception entry stubs, serial diagnostics, PIC remapping, PIT setup, and IRQ entry/dispatch code exist in the source tree.
- Phase 3 now includes a user-reported QEMU BIOS run of a private process address-space path, USER/RW/NX stack validation, CR3 activation, CPL3 entry, syscall probe, exit and page reclamation (2026-11-21).
- The Ring 0 scheduler is a two-task cooperative scheduler with an assembly context switch. It is not a preemptive scheduler.
- The current process runtime does not establish complete multi-process isolation, a production syscall ABI, fault-safe user-copy recovery, or complete user-process exception recovery.
- The exception path includes a user-fault exit hook, but full fault recovery and safe process continuation are not established; kernel exceptions remain terminal.
- The keyboard IRQ path does not yet constitute a complete keyboard driver.

- x86_64 入口、GDT 重新加载、TSS 设置、IDT 基础设施、异常入口桩、串口诊断、PIC 重映射、PIT 设置以及 IRQ 入口/分发代码已存在。
- 2026-11-10 的独立 QEMU 里程碑记录了基础 Ring 0 → Ring 3 → Ring 0 往返、用户代码页、用户栈、CR3 激活及 `int 0x80` 测试系统调用。
- Ring 0 调度器是带汇编上下文切换的双任务协作式调度器，尚非抢占式调度器。
- 基础 Ring 3 里程碑不代表完整进程隔离、生产级系统调用 ABI 或可恢复的用户进程故障处理已经完成。
- 当前异常路径主要提供内核诊断并停止运行，不是完整的用户进程故障恢复系统。
- 键盘 IRQ 路径尚不构成完整键盘驱动。

### Memory management / 内存管理

- The physical page allocator parses the Multiboot memory map and has host-side tests for allocation, release, reservations, malformed maps, and related cases.
- The 4 KiB paging manager provides page mapping, unmapping, and query operations.
- `make test-paging` is a host-side test of selected page-table data structures. It does not prove hardware TLB behavior or complete address-space isolation.
- The separate Ring 3 QEMU milestone records activation of a prepared user address space through CR3. This limited milestone must not be generalized into support for multiple independent processes.
- Kernel heap, slab allocation, and a complete virtual-memory/process address-space subsystem remain planned.

- 物理页分配器解析 Multiboot 内存映射，并具有覆盖分配、释放、保留区间、错误映射等场景的主机侧测试。
- 4 KiB 分页管理器提供页面映射、解除映射和查询操作。
- `make test-paging` 是对部分页表数据结构执行的主机侧测试，不能证明硬件 TLB 行为或完整地址空间隔离。
- 独立的 Ring 3 QEMU 里程碑记录了通过 CR3 激活已准备的用户地址空间；这一有限成果不能扩大解释为已经支持多个独立进程。
- 内核堆、Slab 分配器和完整虚拟内存/进程地址空间子系统仍待实现。

### Filesystems and interfaces / 文件系统与接口

- VFS provides a directory tree, path lookup, file operation dispatch, and filesystem registration/mount interfaces.
- RAMFS can be registered and mounted through VFS; host-side tests cover basic directory/file operations and mount traversal.
- Public headers under `include/qmjy/` define cross-subsystem contracts. The interface-header test checks compile-time compatibility only, not runtime completeness.
- Persistent filesystems, block storage drivers, and a complete storage stack remain planned.

- VFS 已提供目录树、路径查找、文件操作分发以及文件系统注册/挂载接口。
- RAMFS 可通过 VFS 注册和挂载；主机侧测试覆盖基本目录/文件操作及挂载点路径访问。
- `include/qmjy/` 下的公共头文件定义跨子系统接口契约。接口头文件测试只检查编译期兼容性，不证明运行时实现完整。
- 持久化文件系统、块设备存储驱动和完整存储栈仍待实现。

## QBuild and build workflow / QBuild 与构建流程

QBuild version **1.0.0** is the unified configuration, module-management, planning, build, test, diagnostics, and reporting entry point. The existing Makefile remains the actual build execution foundation.

QBuild **1.0.0** 是统一的配置、模块管理、构建规划、构建、测试、诊断和报告入口。现有 Makefile 仍是实际执行构建的基础。

### Supported target / 当前支持目标

- CI-covered: `x86_64` + `bios`, and initial `x86_64` + `uefi`. UEFI is experimental and not release-ready.
- Not supported: `aarch64` + `bios/uefi`. QBuild rejects these targets.

### Common commands / 常用命令

Run from the repository root:

```sh
./tools/qbuild/qbuild help
./tools/qbuild/qbuild info
./tools/qbuild/qbuild config --arch x86_64 --boot bios
./tools/qbuild/qbuild modules
./tools/qbuild/qbuild plan --arch x86_64 --boot bios
./tools/qbuild/qbuild build --arch x86_64 --boot bios --jobs 4
./tools/qbuild/qbuild test --arch x86_64 --boot bios --jobs 4
./tools/qbuild/qbuild clean
```

Direct Makefile build remains available:

```sh
make -B -j4 all
```

QBuild validates `config/modules.conf`, `config/modules.dep`, and `config/modules.status`, including dependency cycles, source mappings, unique object mappings, and enabled-module dependencies. Module status values are `planned`, `experimental`, and `implemented`; verification evidence is tracked separately.

QBuild 会校验 `config/modules.conf`、`config/modules.dep` 和 `config/modules.status`，包括依赖循环、源码映射、对象映射唯一性和启用模块的依赖。模块状态仅使用 `planned`、`experimental` 和 `implemented`；验证证据单独记录。

QBuild currently integrates with the existing Makefile. It is not yet a fully independent module linker, dynamic module loader, or complete interactive configuration system.

QBuild 当前仍集成现有 Makefile，还不是完全独立的模块链接系统、动态模块加载器或完整交互式配置系统。

## Tests and evidence / 测试与证据边界

| Test | What it verifies | What it does not prove |
|---|---|---|
| `test-memory` | Host-side physical allocator behavior | All physical-memory behavior on hardware |
| `test-paging` | Selected host-side page-table data structures | CR3/TLB hardware behavior or full isolation |
| `test-vfs` | VFS host-side operations | Persistent storage or hardware drivers |
| `test-ramfs` | RAMFS and VFS integration on the host | Persistent filesystem support |
| `test-interfaces` | Public header compile compatibility | Runtime implementation of every interface |
| `test-boot` | BIOS image/static checks plus BIOS/QEMU runtime checks and QFS IDE test (requires QEMU) | Hardware compatibility, complete boot-manager features or security |
| `test-process-runtime` | ELF-to-process runtime/address-space and stack validation under host harness | Complete hardware process isolation |
| `test-scheduler-aspace` | Task/address-space CR3 behavior in host harness | Concurrent process scheduling or hardware TLB correctness |
| `test-syscall-x86_64-abi` | x86_64 syscall trap-frame mapping | Production syscall ABI or security model |

The full QBuild test command runs the currently integrated tests. The unified QTest runtime framework remains planned; see [tests/qtest/README.md](tests/qtest/README.md). A skipped test is not a pass.

QBuild 完整测试命令会运行当前已接入的测试。统一的 QTest 运行时框架仍处于规划阶段，详见 [QTest 开发计划](tests/qtest/README.md)。测试被跳过不等于通过。

The 2026-11-11 Phase 1 acceptance verified shell/Python syntax, QBuild configuration, the full kernel build, memory/paging/VFS/RAMFS host tests, interface-header compilation, boot-image static checks, documentation entry points, ignore rules, and Git whitespace checks. The separate 2026-11-10 Ring 3 record documents its QEMU runtime milestone. Neither record claims a complete production boot or security validation.

2026-11-11 的第一阶段验收覆盖了 Shell/Python 语法、QBuild 配置、完整内核构建、内存/分页/VFS/RAMFS 主机侧测试、接口头文件编译、启动映像静态检查、文档入口、忽略规则和 Git 空白检查。独立的 2026-11-10 Ring 3 记录描述了对应的 QEMU 运行里程碑。两份记录都不代表完整生产级启动或安全验证已经完成。

## Language editions / 语言版本

qmjyOS defines two separate editions:

- **Chinese edition:** Simplified Chinese by default across boot menus, installer, recovery, system UI, and ordinary system messages.
- **International edition:** English by default across boot menus, installer, recovery, system UI, and ordinary system messages.

Technical identifiers, source symbols, protocol names, and developer diagnostics may remain language-neutral. The policy is documented in [LANGUAGE_POLICY.md](docs/internationalization/LANGUAGE_POLICY.md). The policy document defines requirements; it does not mean both editions are already fully implemented or translated.

qmjyOS 规划两个独立版本：

- **中文版：** 启动菜单、安装器、恢复环境、系统界面和普通系统消息默认使用简体中文。
- **国际版：** 启动菜单、安装器、恢复环境、系统界面和普通系统消息默认使用英文。

技术标识符、源码符号、协议名称和开发诊断信息可以保持语言中立。具体政策见 [语言版本政策](docs/internationalization/LANGUAGE_POLICY.md)。政策文档定义的是目标要求，不代表两个版本已经全部实现或完成翻译。

## Current development direction / 当前开发方向

The active focus is **Phase 3 — Runtime Integration and Hardening**.
Phase 1 is accepted; Phase 2 kernel-infrastructure foundations are in place.
Phase 3 has an experimental ELF64 process-runtime path, but is not complete.

Current priorities:

1. Fault-safe user-memory access and user-exception recovery.
2. Per-task kernel stacks and process-runtime scheduler integration.
3. Multiple independent user address spaces and concurrent process scheduling.
4. Stable syscall ABI, validation rules, and authorization.
5. Timer-driven preemption and later SMP.
6. QBoot BIOS completion and UEFI hardening.
7. QEMU regression evidence and security review.

当前重点为第三阶段：运行时集成与加固。第三阶段已有实验性 ELF64 进程运行时路径，但尚未完成。

These are remaining development goals, not claims of completed features.

## Repository layout / 仓库结构

```text
.
├── arch/             # Architecture-specific code and contracts
├── boot/             # QBoot boot paths and handoff protocol
├── config/           # Build and module configuration
├── docs/             # Architecture, development, and policy documents
├── drivers/          # Device drivers
├── fs/               # VFS and filesystem implementations
├── include/qmjy/     # Public kernel interfaces
├── kernel/           # Kernel core
├── mm/               # Physical memory and paging
├── sched/            # Task scheduling and context switching
├── tests/            # Host tests, interface tests, and QTest plan
├── tools/qbuild/     # QBuild tool and documentation
└── Makefile          # Existing build and test integration
```

See [SYSTEM_LAYOUT.md](docs/architecture/SYSTEM_LAYOUT.md) for directory responsibilities. Existing paths remain authoritative; documentation does not require moving source files.

## License and security / 许可证与安全

No license has been selected. All rights are reserved by default until a license is added. Do not assume redistribution or reuse is permitted. qmjyOS is experimental and must not be used to protect sensitive data.

项目尚未选择许可证。在正式添加许可证之前，默认保留所有权利。请勿自行假定允许再分发或复用。qmjyOS 属于实验性项目，不应用于保护敏感数据。

<!-- QMjyOS-DOCS-CI-14 -->


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


<!-- QBUILD-1.0.0-2026-11-16 -->

## QBuild 1.0.0 — Current build-system baseline

QBuild 1.0.0 provides the stable build and project-management CLI on top of the v0.5.0 foundation. The 1.0.0 implementation includes guarded module enable/disable operations, integrity-checked configuration snapshots and rollback-aware restore, machine-readable build/test reports, toolchain diagnostics, and QBuild's own regression suite. The existing Makefile remains the actual kernel build and test backend. CI now covers x86_64 + BIOS and an initial x86_64 + UEFI path. AArch64, dynamic module loading, and a complete QTest runtime are not claimed. QBuild 1.0.0 is a separate tool version and does not change the planned qmjyOS 0.1.0 system release.

QBuild 1.0.0 的开发目标是在 v0.5.0 基础上提供稳定的构建与工程管理入口，新增带依赖保护的模块启停、带完整性校验的配置快照与回滚恢复、机器可读构建/测试报告、工具链诊断和 QBuild 自身回归测试。内核真实构建与测试仍由现有 Makefile 执行。CI 已覆盖 x86_64 + BIOS 和初步 x86_64 + UEFI；不宣称 AArch64、动态模块加载或完整 QTest 运行时已经实现。QBuild 版本独立于 qmjyOS 0.1.0 系统发布版本。

## Phase 3 process-runtime regression tests / 第三阶段进程运行时回归测试

The host suite includes `test-process-runtime`, `test-scheduler-aspace`, and `test-syscall-x86_64-abi`. BIOS and UEFI QEMU runtime checks require the private CR3, CPL3, process-exit/page-reclamation markers as well as scheduler progress. See [Phase 3 Runtime Validation](docs/development/PHASE3_RUNTIME_VALIDATION_2026-11-21.md).

主机侧测试包含 `test-process-runtime`、`test-scheduler-aspace` 和 `test-syscall-x86_64-abi`。BIOS/UEFI QEMU 运行检查要求私有 CR3、CPL3、进程退出/页面回收标志以及调度器持续运行。详见[第三阶段运行时验收](docs/development/PHASE3_RUNTIME_VALIDATION_2026-11-21.md)。
