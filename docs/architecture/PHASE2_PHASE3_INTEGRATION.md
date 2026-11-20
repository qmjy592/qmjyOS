# qmjyOS Phase 2 + Phase 3 integration package

## Base and intent

This package targets GitHub `qmjy592/qmjyOS` branch `main`, base commit:

`b51644cd0c708cac55eecd0b5ba52e04043fbabc`

It builds on the already-applied Foundation, MM_HEAP, BIOS NXE, QFS/MBR, QBuild and Phase 13 storage repairs. It does not rerun the old v3 installers. The new installer performs exact source-anchor checks, writes backups, stages files beside their targets, and uses atomic replacement.

## Phase 2 integration added here

1. A bounded Multiboot v1 adapter constructs the versioned `qm_boot_info_t` record from the existing BIOS bootstrap handoff. It validates the memory map records, range arithmetic, overlaps, capacity and unsupported loaded modules. The resulting memory-map array is kept in kernel storage whose physical address is passed explicitly.
2. `arch/x86_64/boot/entry64.S` installs an early exception IDT after `.bss` clearing and before calling `main`. Its selector is explicitly `0x08`, matching the actual Multiboot bootstrap GDT and the kernel GDT; no obsolete `0x18` assumption is made. The exception stub passes vector, error code, RIP, CS and RFLAGS using the x86_64 SysV argument registers. The panic reporter writes to debugcon first and only mirrors to COM1 after `serial_init`.
3. `kernel/core/main.c` registers ordered callbacks with the existing namespaced kernel init registry. Boot diagnostics, GDT/TSS/IDT/PIC/PIT, physical memory, bounded heap, paging self-test, VFS/RAMFS, syscall registration and QFS storage setup are now invoked through ordered init levels.
4. At this package snapshot, Ring 3 exit was routed through `qm_syscall_dispatch()`; the later 2026-11-21 runtime adds test echo/exit dispatch and private address-space activation (see the update section below).

## Phase 3 foundations added here

1. A strict ELF64 x86_64 `ET_EXEC` PT_LOAD loader core. It validates program headers, user virtual range, arithmetic, file/memory sizes, page overlap, entry location, and W^X. Mapping is delegated to platform callbacks. On a mapping failure, previously mapped segments are unwound.
2. A bounded process metadata registry that stores loaded image descriptors and exposes READY → RUNNING → EXITED → REAPED transitions.
3. Host tests cover the ELF loader's BSS zeroing, W^X rejection, overlapping segment rejection and mapping rollback, as well as process metadata transitions.

## Deliberate remaining boundaries

This is a meaningful integration tranche, not a claim that the whole OS is complete:

- The BIOS bootstrap still uses Multiboot v1. The adapter creates Boot Info inside the kernel; the future QBoot manager must construct and pass Boot Info directly according to the final handoff contract.
- The current adapter rejects Multiboot modules. Command-line, ACPI, framebuffer and firmware system-table records are not populated yet.
- The adapter constructs and validates `qm_boot_info_t`, but the current PMM callback still calls the legacy `mm_init_from_multiboot()` parser. A PMM API that consumes the normalized Boot Info memory-region array is not claimed in this tranche.
- At this package snapshot, the ELF loader still needed a process address-space backend; the 2026-11-21 runtime update connects the loader to a private address-space root, user stack setup, CR3 activation and CPL3 entry.
- At this package snapshot, the process registry owned metadata only. The later runtime adds a bounded process runtime and page reclaim, but does not yet provide concurrent multi-process scheduling or complete process/thread management.
- `THREAD` and preemptive scheduling remain planned. The existing scheduler remains two fixed cooperative Ring 0 tasks.
- The later runtime registers test echo and exit handlers and validates syscall trap-frame register mapping. User pointer prevalidation exists, but direct copy remains not fault-safe; production ABI, per-process authorization and complete user fault recovery remain future work.
- IPC, capability and service components remain separately built host-tested foundations; they are not yet launched as user processes.
- `RUNTIME`, `SYSTEM_SERVICES`, `QSHELL`, applications and complete init service launch remain incomplete. Initial x86_64 UEFI is build/OVMF-tested in CI but is not release-ready; AArch64 remains unsupported.
- QFS journal/intent log and crash replay recovery remain outstanding.

## Verification plan

The installer can perform source-anchor, pre-existing-repair, module-map, and shell/Python syntax checks. The package validation pass also compiles the early-entry/exception assembly and its freestanding C helpers where the local toolchain is available. After applying on Termux, run:

```sh
python3 tools/qbuild/module_plan.py --check
make test-kernel-foundation
make test-elf64
make test-process
make test-heap
make test-interfaces
make -j2 all
./tools/qbuild/qbuild test --arch x86_64 --boot bios --jobs 2
```

The final command includes the existing QBoot BIOS, QFS IDE and other tests. The BIOS disk boot and `tools/qemu_boot_test.sh` serial/debugcon evidence is retained under `build/test-evidence/` (QFS IDE evidence remains under its existing `build/phase12-unified/` run directory). These paths avoid `/tmp`/`TMPDIR` and are compatible with the project's Termux constraint. Each run uses a unique ID and refuses to overwrite existing logs. These tests require the relevant Termux tools/QEMU and must be reported from their real output. This package itself does not claim a QEMU run.

<!-- QMjyOS-PHASE2-PHASE3-STATUS-UPDATE-2026-11-16 -->

## Phase 2 / Phase 3 Status Clarification — 2026-11-16

### 中文：实现边界

本节用于明确阶段集成进度，避免将基础组件、
实验性路径和完整产品能力混为一谈。

**启动与内存：**

- 当前 x86_64 BIOS 启动路径已有构建和启动测试基础。
- Multiboot 启动信息处理与物理内存初始化需要分别验收。
- ELF64 加载相关功能不能自动等同于完整独立地址空间。
- 页面权限、用户页映射和进程级 CR3 隔离必须单独验证。

**任务和进程：**

- 当前 Ring 0 双任务交替测试属于协作式调度。
- 抢占式调度仍需要单独实现和测试。
- 进程 metadata registry 不能代替完整进程管理。
- 独立地址空间、进程切换、线程生命周期和资源回收
  均需要独立验收。

**Ring 3：**

- 基础 CPL3 到 CPL0 特权级转换测试已有记录。
- 完整用户空间隔离和稳定 syscall ABI 仍需继续完成。
- 不应将一次特权级转换测试描述为完整用户态环境。

**文件系统：**

- QFS 的主机端测试和崩溃一致性检查应以实际测试结果为准。
- 持久化、日志、恢复和崩溃重放功能必须分别有实现证据。
- 不应仅凭构建成功推断所有断电恢复场景均可靠。

### CI 扩展记录

本轮新增源码语法、ELF 布局、不支持目标拒绝、
ASan/UBSan、QFS 和 QEMU 检查。

本地已通过的检查包括源码语法、QBuild 构建、
ELF 布局、QBuild 测试、ASan/UBSan 和
git diff --check。

Termux 不支持 LeakSanitizer，内存泄漏检测未完成。

Run #14：
https://github.com/qmjy592/qmjyOS/actions/runs/43826588727

最终 CI 状态需要独立核实。

### English: Integration Boundaries

This section separates foundational components,
experimental paths, and complete product capabilities.

The current x86_64 BIOS path has build and boot-test
foundations. Multiboot information handling and physical
memory initialization require separate verification.
ELF64 loading support does not automatically provide
independent process address spaces.

The Ring 0 two-task test demonstrates cooperative scheduling,
not preemptive scheduling. The process metadata registry
is not a complete process manager.

A basic CPL3-to-CPL0 transition test does not establish
complete user-space isolation or a stable syscall ABI.

QFS persistence, logging, recovery, and crash replay must
each be supported by implementation and test evidence.
A successful build alone does not prove power-loss safety.

The expanded CI includes source syntax, ELF layout,
unsupported target rejection, ASan/UBSan, QFS, and QEMU.
The final Run #14 result must be verified independently.

## Phase 3 runtime integration update — 2026-11-21

This section supersedes the earlier Phase 3 boundary statements above where they say that a per-process CR3/user stack/runtime path is not connected. The earlier text remains as a historical description of the integration package at that time.

### Implemented path

- ELF64 loader core is integrated with the process runtime.
- A private address-space root is created and the user stack is mapped with USER/RW/NX permissions.
- Runtime checks the stack mapping and activates the process CR3 before entering CPL3.
- `int 0x80` syscall entry uses the x86_64 trap frame; test handlers include echo/return and process exit.
- Exit restores the kernel context and reclaims runtime pages.
- Scheduler address-space/CR3 switching has host regression coverage.

### Latest evidence and boundaries

The user reported the QEMU BIOS serial output passing these markers on Termux: `Private PML4 and USER/RW/NX stack: ready`, `Private CR3 activated. Entering CPL3`, `ELF process exit and page reclamation: PASS`, and `Basic privilege transition: PASS`. The raw log was not present in the uploaded review snapshot, so this is user-reported runtime evidence rather than independently replayed evidence in this review container.

Host regression targets include `test-process-runtime`, `test-scheduler-aspace`, and `test-syscall-x86_64-abi`. They do not prove hardware TLB behavior, safe recovery from arbitrary user faults, concurrent process scheduling, or production syscall semantics. User-copy validation currently preflights mappings and then directly copies; complete fault-safe copy recovery is still outstanding.

Remaining Phase 3 work includes fault-safe user copy, broader user exception recovery, process concurrency, per-task kernel stack/context integration, preemptive scheduling, syscall authorization and ABI stabilization, and security review.
