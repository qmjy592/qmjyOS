# Phase 1 - System Skeleton Completion Plan

## Goal

Establish a coherent, extensible qmjyOS system skeleton.

This phase defines system boundaries and public contracts.
It does not claim that every subsystem is implemented.

## Work Areas

1. Architecture specifications
2. Common types and error conventions
3. Public kernel interfaces
4. Architecture abstraction
5. Memory and task interfaces
6. Device and storage interfaces
7. Filesystem interfaces
8. User-space and syscall interfaces
9. QBoot handoff protocol
10. QBuild and QTest integration
11. Module responsibilities and dependencies
12. Documentation and implementation status

## Acceptance Requirements

- Directory responsibilities are documented.
- Public interfaces have consistent type conventions.
- Architecture-specific code is isolated.
- Module dependencies are explicit.
- QBoot has a versioned handoff protocol.
- QBuild can describe and manage the supported build targets.
- QTest has discoverable test entry points.
- Implemented, partial and planned functionality are distinguished.
- Chinese and international edition policies are documented.
- Existing working kernel functionality remains intact.

## Non-Goals

Phase 1 does not require:

- A complete desktop environment
- A complete networking stack
- A complete package manager
- Full UEFI support
- Full AArch64 boot support
- A production-ready filesystem
- A complete userspace ABI
- Dynamic kernel module loading

## Final Review

The phase must undergo a unified review covering:

- Header compatibility
- Type and macro consistency
- Build integration
- Module dependency consistency
- Test coverage
- Documentation accuracy
- Existing functionality regression
- Git working tree and change history

Only after this review should Phase 1 be marked complete.

## Final Acceptance Record — 2026-11-11

The first system-foundation phase has passed its final local validation.

### Verified results

- Shell syntax check for `tools/qbuild/qbuild`: PASS
- Python syntax check for `tools/qbuild/module_plan.py`: PASS
- QBuild x86_64 + BIOS configuration and module mapping checks: PASS
- Full kernel build with `make -B -j4 all`: PASS
- QBuild test suite: PASS
- Physical memory allocator host tests: PASS
- Paging self-test: PASS
- VFS host tests: PASS
- RAMFS host tests: PASS
- Public interface header compilation test: PASS
- BIOS boot image static verification: PASS
- Documentation entry-point checks: PASS
- Build artifact ignore checks: PASS
- `git diff --check`: PASS

### QBuild module inventory at acceptance

- Total modules: 51
- Modules with source mappings: 9
- Planned modules: 14
- Status inventory: 1 implemented, 9 experimental, 41 planned

These figures describe module metadata and source mapping. They do not mean that all listed modules are fully implemented.

### Scope and limitations

This acceptance confirms the current system foundation, build integration,
host-side tests, interface checks, and static boot-image checks.

It does not claim completion of:

- A full production QBoot BIOS boot manager
- UEFI boot support
- AArch64 boot support
- QEMU or physical-hardware boot verification
- Complete user-mode isolation and system-call implementation
- A complete runtime test framework

The current BIOS boot path remains a development bootstrap.

The repository worktree is intentionally left uncommitted until the
maintainer reviews and publishes this phase.
