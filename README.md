# qmjyOS — 自研实验性操作系统 / Experimental Operating System

qmjyOS is a self-developed experimental operating-system project, primarily written in freestanding C and x86 assembly. The current development baseline targets x86_64 with a BIOS development bootstrap. The project is building a modular kernel foundation, architecture boundaries, public interfaces, memory management, interrupt handling, filesystems, scheduling, and its own boot infrastructure named QBoot.

qmjyOS 是一个自研的实验性操作系统项目，主要使用 freestanding C 和 x86 汇编开发。当前开发基线为 x86_64 + BIOS 开发引导链，正在逐步建设模块化内核基础设施、架构边界、公共接口、内存管理、中断处理、文件系统、任务调度以及自研启动体系 QBoot。

> **Project status / 项目状态:** Phase 1 — System Foundation completed and uploaded on 2026-11-11. The project remains experimental, is not production-ready, and has not undergone a complete security audit.
>
> **当前状态：** 第一阶段“系统骨架建设”已于 2026-11-11 完成验收并上传仓库。项目仍处于实验开发阶段，不适合生产环境，尚未完成全面安全审计。

## Documentation / 文档导航

### Project and development / 项目与开发
- [Phase 1 Completion and Acceptance / 第一阶段完成与验收](docs/development/PHASE1_COMPLETION.md)
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

### QBuild v0.5.0 module inventory / 模块统计

| Item | Acceptance value |
|---|---:|
| Total modules | 51 |
| Modules with source mappings | 9 |
| Modules in the current build plan | 14 |
| Status: implemented | 1 |
| Status: experimental | 9 |
| Status: planned | 41 |

These are separate metadata counts: source mappings, current build-plan membership, and implementation status are not interchangeable. A header, directory, dependency entry, or successful compile does not by itself make a subsystem fully implemented.

以上是不同维度的统计：源码映射数量、当前构建计划模块数量和实现状态数量不能混为一谈。仅有头文件、目录、依赖条目或编译成功，并不意味着子系统已经完整实现。

## Current implementation snapshot / 当前实现概况

The following describes the current source tree and documented test evidence. It distinguishes implemented code from limited tests and planned work.

以下内容描述当前源码树和已有测试证据，并区分已存在的代码、有限范围的测试以及仍在规划的工作。

### Boot and architecture / 启动与架构

- The current supported build target is **x86_64 + BIOS**.
- The BIOS path contains a Multiboot-based 32-bit development bootstrap and a transition into the 64-bit kernel image.
- Early boot establishes an identity mapping for the first 1 GiB using 2 MiB pages.
- The current bootstrap is not the complete QBoot BIOS boot manager.
- x86_64 UEFI and AArch64 boot/build support remain planned and are rejected as unsupported build targets by the current QBuild configuration.
- QBoot is the project-owned boot-manager direction; GRUB or another third-party boot manager is not the intended final boot manager.

- 当前受支持的构建目标是 **x86_64 + BIOS**。
- BIOS 路径包含基于 Multiboot 的 32 位开发引导程序，并切换到 64 位内核映像。
- 早期启动使用 2 MiB 页建立前 1 GiB 的恒等映射。
- 当前 bootstrap 还不是完整的 QBoot BIOS 启动管理器。
- x86_64 UEFI 与 AArch64 启动/构建仍处于规划阶段，当前 QBuild 会明确拒绝尚未实现的目标。
- QBoot 是项目计划自研的启动管理器，不以 GRUB 或其他第三方启动管理器作为最终方案。

### CPU, interrupts, and scheduling / CPU、中断与调度

- x86_64 entry code, GDT reload, TSS setup, IDT infrastructure, exception entry stubs, serial diagnostics, PIC remapping, PIT setup, and IRQ entry/dispatch code exist in the source tree.
- A basic Ring 0 → Ring 3 → Ring 0 path, a user code page, a user stack, CR3 activation, and an `int 0x80` test syscall have a separate QEMU milestone record dated 2026-11-10.
- The Ring 0 scheduler is a two-task cooperative scheduler with an assembly context switch. It is not a preemptive scheduler.
- The basic Ring 3 milestone does not establish complete process isolation, a production syscall ABI, or recoverable user-process fault handling.
- The exception path currently provides kernel diagnostics and halt behavior; it is not a complete user-process recovery system.
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

QBuild version **0.5.0** is the unified configuration, module-validation, planning, build, and test entry point. The existing Makefile remains the actual build execution foundation.

QBuild **0.5.0** 是统一的配置、模块校验、构建规划、构建和测试入口。现有 Makefile 仍是实际执行构建的基础。

### Supported target / 当前支持目标

- Supported: `x86_64` + `bios`.
- Not yet supported: `x86_64` + `uefi`, and `aarch64` targets.

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
| `test-boot` | Static checks of generated BIOS boot images | Actual BIOS/UEFI boot in QEMU or on hardware |

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

## Next development direction / 下一阶段方向

Phase 2 is expected to focus on kernel infrastructure and runtime correctness, including:

1. Kernel initialization and subsystem ordering.
2. Completing physical and virtual memory management, including kernel heap allocation.
3. Strengthening GDT, TSS, IDT, exception, and interrupt handling.
4. Building process/thread foundations and improving the scheduler, eventually adding preemption.
5. Defining and validating a stable system-call ABI and user address-space lifecycle.
6. Extending device, block-storage, and filesystem infrastructure.
7. Expanding automated tests and QEMU runtime verification.

第二阶段预计转向内核基础设施与运行时正确性，包括内核初始化和子系统顺序、物理/虚拟内存与内核堆、GDT/TSS/IDT/异常/中断、进程线程与调度器、系统调用 ABI、设备/块存储/文件系统，以及更完整的自动化测试和 QEMU 运行验证。

These are development goals, not current implementation claims.

以上均为后续开发目标，不是当前已实现功能的声明。

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
