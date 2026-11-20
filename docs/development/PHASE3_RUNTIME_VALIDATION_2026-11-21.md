# qmjyOS Phase 3 Runtime Validation / 第三阶段运行时验收

**Date:** 2026-11-21

**Target:** x86_64 BIOS development boot, QEMU runtime
**Status:** User-reported QEMU runtime PASS; host regression PASS in review container; GitHub CI pending after sync.

## Verified path / 已验证路径

1. ELF64 image is loaded through the process runtime.
2. A private PML4/address-space root is prepared.
3. User stack permissions are checked as USER/RW/NX.
4. Private CR3 is activated before CPL3 entry.
5. The Ring 3 probe exercises `int 0x80` echo/return and exit.
6. Process exit restores the kernel execution context and reclaims runtime pages.
7. The Ring 0 cooperative scheduler continues after the test.

## User-reported QEMU BIOS markers

The latest serial log reported by the user contains these successful markers:

```text
[Ring 3] Private PML4 and USER/RW/NX stack: ready.
[Ring 3] Private CR3 activated. Entering CPL3.
[Ring 3] ELF process exit and page reclamation: PASS.
[Ring 3] Basic privilege transition: PASS.
```

**Evidence note:** the original serial log itself was not included in the uploaded source review archive. This entry preserves the reported markers, not a replayed log artifact.

## Host regression targets

- `make test-process-runtime` — ELF process runtime/address-space setup and lifecycle.
- `make test-scheduler-aspace` — task/address-space CR3 handling under host harness.
- `make test-syscall-x86_64-abi` — syscall trap-frame register and ABI mapping.

The QBuild suite includes these tests. Host PASS does not prove CPU page-table hardware behavior or all concurrent edge cases.

## CI coverage to be updated

- Build host-test executables on the runner; never execute checked-in Android ARM64 test binaries.
- BIOS QEMU serial checks must require all Phase 3 runtime markers and scheduler progress.
- UEFI OVMF checks must require UEFI handoff markers, the same Phase 3 runtime markers, and scheduler progress.
- Upload serial/debug logs and QBuild test reports as workflow artifacts.

## Known remaining work / 尚未完成

- Fault-safe user-copy recovery (current code prevalidates page permissions then copies directly).
- Complete user exception recovery and reliable process continuation.
- Concurrent independent user processes and scheduler integration with per-task kernel stacks.
- Preemptive scheduling and SMP.
- Stable, documented production syscall ABI and authorization.
- Full security audit and broader hardware/firmware compatibility.

## Review-container checks

- `make -B -j2 all`: PASS.
- QBuild self-test (17 tests): PASS.
- QBuild configuration/module plan: PASS (58 modules; 22 source mappings; 17 in the build plan).
- All host regression targets: PASS after rebuilding into `build/tests/host/`; this also removes the architecture-mismatch risk from tracked Android ARM64 binaries.
- `tools/ci/run_host_sanitizers.sh`: PASS (ASan + UBSan; LeakSanitizer not enabled).
- Phase 3 marker contract, BIOS NXE test, QBoot GDT/early-IDT test: PASS.
- UEFI ELF64 layout test: PASS (3 page-aligned, non-overlapping PT_LOAD segments).
- BIOS ELF/boot static verifier: PASS using host `readelf`/`nm` compatibility wrappers because the container lacks `llvm-readelf`/`llvm-nm`; QBoot BIOS image builder: PASS.
- UEFI target created `BOOTX64.EFI` and `kernel64-uefi.elf`; the target stopped at its final inspection command because `llvm-readobj` is unavailable. `file` confirmed a PE32+ x86-64 EFI application; UEFI ELF layout test passed separately.
- QEMU runtime: not executed in the review container (QEMU unavailable).
- GitHub Actions: not yet run for this snapshot.
