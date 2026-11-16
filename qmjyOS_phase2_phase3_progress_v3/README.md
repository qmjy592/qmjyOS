# qmjyOS Phase 2 / Phase 3 incremental package — v3

This package is an incremental implementation/review tranche. It does **not** claim that all of Phase 2 or Phase 3 is finished.

## Apply all included work

Extract the full `qmjyOS_phase2_phase3_progress_v3` directory under the qmjyOS repository root, then run from the repository root:

```sh
bash qmjyOS_phase2_phase3_progress_v3/apply_phase2_all_v3.sh
```

The wrapper runs the Phase 2 foundation and MM_HEAP installers, applies the BIOS NXE duplicate-check cleanup, rebuilds the full x86_64 + BIOS target with `make -j2 all`, and runs `git diff --check`.

Each installer verifies its exact anchors before writing, backs up changed files under `build/repair-backups/`, uses atomic file replacement, and supports idempotent re-runs. The scripts do not use `/tmp`, do not invoke `git reset/checkout/clean`, and do not overwrite the local QFS diagnostic-preservation behavior.

To apply only the new BIOS NXE cleanup after the earlier Phase 2 work is already present:

```sh
bash qmjyOS_phase2_phase3_progress_v3/phase2_boot/apply_phase2_boot.sh
```

## Included progress

### Phase 2 foundation tranche

- Versioned boot-info validation primitives.
- Staged initialization registry and failure propagation primitives.
- Fixed-size syscall registration/dispatch table.
- Host tests for the new primitives.

These remain foundations until wired to the actual boot handoff and Ring 3 entry/return path.

### MM_HEAP tranche

- 256 KiB bounded early kernel heap, 16-byte aligned.
- First-fit allocation, split/coalesce, calloc zeroing, stats, overflow checks, invalid-pointer and double-free rejection.
- Startup initialization immediately after serial initialization.
- QBuild source mapping, `test-heap`, public-header compatibility test.
- Remains `experimental`: no PMM/VMM growth, no lock, and not IRQ/concurrent-task safe.

### BIOS NXE cleanup tranche

- Removes one redundant earlier NX feature-detection / EFER.NXE sequence from `boot/bios/boot.S`.
- Retains the later branch that preserves EBX and enables LME+NXE only when CPUID reports NX; the fallback enables LME without NXE.
- Adds a source-level regression check to `make test-boot` and boot feature documentation.
- Does not change Multiboot register saving, CR4.PAE, CR0.PG/WP, GDT loading, or the long-mode far jump.

## Evidence and limits

Verified while preparing the package:

- Heap tests with Clang strict warnings: PASS.
- Heap tests under AddressSanitizer and UndefinedBehaviorSanitizer: PASS.
- Heap tests with GCC strict warnings: PASS.
- Heap freestanding x86_64 object compilation: PASS; no undefined symbols.
- BIOS NXE static regression test: PASS.
- BIOS bootstrap i386 assembly compilation: PASS.
- BIOS NXE installer first application: PASS.
- BIOS NXE installer idempotent second application: PASS (`[ALREADY]`).
- Installer scripts' Bash syntax and the test's Python syntax: PASS.
- Final ZIP integrity: PASS.

The integrated wrapper runs the actual full kernel build in the target qmjyOS repository after applying all changes. This preparation environment did not run the user's complete repository build or QEMU, so no full-kernel or QEMU runtime pass is claimed here.

## Still open

Phase 2 still needs actual staged-init integration with the versioned boot handoff, PMM-backed paging pages, a growing kernel virtual heap, full exception/IRQ frame validation and user-fault containment, process/thread lifecycle, preemptive scheduling, and a real user syscall ABI. Phase 3 still needs the ELF64 loader, independent process address spaces, unified process-owned IPC/capabilities, user runtime and init, service lifecycle integration, Shell, pseudo-filesystems, and end-to-end runtime tests. See `PHASE2_PROGRESS_V3.md` and `PHASE2_PHASE3_GAP_AUDIT.md`.
