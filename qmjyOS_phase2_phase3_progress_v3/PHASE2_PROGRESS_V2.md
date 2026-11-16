# qmjyOS Phase 2 progress — v2

## Current confirmed state

1. Phase 1 system skeleton remains the existing baseline; no directory-wide rewrite is introduced.
2. Phase 2 foundation tranche previously supplied boot-info validation, init registry, syscall registration/dispatch and host tests.
3. This v2 tranche implements the first `MM_HEAP` source and connects its initialization to `kernel/core/main.c`.

## Why MM_HEAP is only experimental

The current allocator reserves 256 KiB in `.bss` and is not PMM-backed. It is safe as an early bounded allocator under serialized access, but does not yet satisfy the final dynamic memory subsystem contract. `MM_HEAP` therefore stays `experimental`; `MM_SLAB`, `MM_VIRTUAL`, PROCESS, THREAD and preemption stay at their existing maturity states.

## Still open in Phase 2

- Convert the Multiboot v1 boot information to the versioned `qm_boot_info_t` and route startup through `qm_kernel_init()` without changing the verified boot order.
- Replace the static page-table pool with PMM-owned pages and explicit physical/virtual ownership. Current bootstrap direct identity access is limited to the first 1 GiB.
- Define a kernel virtual address layout and complete the heap growth path over PMM/VMM.
- Normalize exception/IRQ frames, validate CPL3 fault containment and add per-task TSS.RSP0 handling.
- Build PCB/TCB, ready/blocked queues, task exit/reaping, then test preemption from an interrupt-frame context (never by calling normal `switch_to` on an unprepared IRQ frame).
- Wire the syscall table to a real user-entry/return ABI, including user-pointer validation/copy-in/copy-out.
- Complete block-device/MBR/QFS error and recovery handling; do not claim power-loss atomicity without journaling or intent-log recovery.

## Still open in Phase 3

- ELF64 loader and validation/rollback.
- Process and thread lifecycle with independent address spaces.
- Kernel IPC handles bound to process ownership and capability checks; current host IPC API and public `qmjy/ipc.h` are not yet a unified runtime contract.
- User runtime/syscall wrappers and `init` as first user process.
- Service lifecycle integrated through IPC and capabilities.
- User-space Shell and system tools.
- devfs/procfs/sysfs and full QBoot → kernel → init → services → shell runtime test.

## Evidence boundary

A passed host allocator test proves only heap behavior under that host harness. The x86_64 object compile proves the freestanding translation unit compiles. The bundled installer runs the full kernel build on the target repository; QEMU/real hardware must be recorded separately. The current work does not claim QEMU runtime success.
