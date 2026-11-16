# qmjyOS Phase 2 + Phase 3 integration package

## Base and intent

This package targets GitHub `qmjy592/qmjyOS` branch `main`, base commit:

`b51644cd0c708cac55eecd0b5ba52e04043fbabc`

It builds on the already-applied Foundation, MM_HEAP, BIOS NXE, QFS/MBR, QBuild and Phase 13 storage repairs. It does not rerun the old v3 installers. The new installer performs exact source-anchor checks, writes backups, stages files beside their targets, and uses atomic replacement.

## Phase 2 integration added here

1. A bounded Multiboot v1 adapter constructs the versioned `qm_boot_info_t` record from the existing BIOS bootstrap handoff. It validates the memory map records, range arithmetic, overlaps, capacity and unsupported loaded modules. The resulting memory-map array is kept in kernel storage whose physical address is passed explicitly.
2. `arch/x86_64/boot/entry64.S` installs an early exception IDT after `.bss` clearing and before calling `main`. Its selector is explicitly `0x08`, matching the actual Multiboot bootstrap GDT and the kernel GDT; no obsolete `0x18` assumption is made. The exception stub passes vector, error code, RIP, CS and RFLAGS using the x86_64 SysV argument registers. The panic reporter writes to debugcon first and only mirrors to COM1 after `serial_init`.
3. `kernel/core/main.c` registers ordered callbacks with the existing namespaced kernel init registry. Boot diagnostics, GDT/TSS/IDT/PIC/PIT, physical memory, bounded heap, paging self-test, VFS/RAMFS, syscall registration and QFS storage setup are now invoked through ordered init levels.
4. The Ring 3 test exit request is routed through `qm_syscall_dispatch()` instead of bypassing the generic syscall table. The sole registered number remains a test-only exit request; it is not a production process-exit ABI.

## Phase 3 foundations added here

1. A strict ELF64 x86_64 `ET_EXEC` PT_LOAD loader core. It validates program headers, user virtual range, arithmetic, file/memory sizes, page overlap, entry location, and W^X. Mapping is delegated to platform callbacks. On a mapping failure, previously mapped segments are unwound.
2. A bounded process metadata registry that stores loaded image descriptors and exposes READY → RUNNING → EXITED → REAPED transitions.
3. Host tests cover the ELF loader's BSS zeroing, W^X rejection, overlapping segment rejection and mapping rollback, as well as process metadata transitions.

## Deliberate remaining boundaries

This is a meaningful integration tranche, not a claim that the whole OS is complete:

- The BIOS bootstrap still uses Multiboot v1. The adapter creates Boot Info inside the kernel; the future QBoot manager must construct and pass Boot Info directly according to the final handoff contract.
- The current adapter rejects Multiboot modules. Command-line, ACPI, framebuffer and firmware system-table records are not populated yet.
- The adapter constructs and validates `qm_boot_info_t`, but the current PMM callback still calls the legacy `mm_init_from_multiboot()` parser. A PMM API that consumes the normalized Boot Info memory-region array is not claimed in this tranche.
- The ELF loader needs an address-space backend. The callback interface is the boundary; it is not yet connected to a per-process CR3 root or a user stack builder.
- The process registry owns metadata only. It does not switch to a process, allocate an independent page-table root, or implement process/thread scheduling.
- `THREAD` and preemptive scheduling remain planned. The existing scheduler remains two fixed cooperative Ring 0 tasks.
- The generic syscall dispatcher is integrated only for the Ring 3 test-exit syscall. User-pointer copy/validation, full register-argument ABI, per-process authorization and recoverable user faults remain future work.
- IPC, capability and service components remain separately built host-tested foundations; they are not yet launched as user processes.
- `RUNTIME`, `SYSTEM_SERVICES`, `QSHELL`, applications, complete init service launch, and QBoot UEFI/AArch64 remain incomplete.
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
