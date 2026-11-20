# QTest

QTest is the planned unified qmjyOS testing framework.

## Current situation

The project currently has host-side test programs driven by Makefile.

Existing test entry points include:

- make test-memory
- make test-paging
- make test-vfs
- make test-ramfs
- make test-boot
- make test-interfaces

These targets are not yet a complete QTest framework.

## Planned responsibilities

QTest will eventually provide:

- Test case registration
- Test suite organization
- Test result reporting
- Pass, fail and skip states
- Assertions
- Test filtering
- Architecture-specific test execution
- Host-side and target-side test separation
- Machine-readable result output

## Public interface

The initial public test declarations are in:

include/qmjy/test.h

The declarations do not mean that the runtime test framework
has already been implemented.

## Verification rules

- A skipped test is not a pass.
- Host tests do not prove hardware behavior.
- Build tests do not prove runtime correctness.
- Target tests must record architecture and execution environment.

- make test-process-runtime
- make test-scheduler-aspace

- `make test-syscall-x86_64-abi` — x86_64 Ring 3 syscall trap-frame argument/return ABI

## Phase 3 regression targets

QBuild currently includes these specific host regression tests in the BIOS suite:

- `test-process-runtime` — process image loading/address-space/stack/runtime lifecycle.
- `test-scheduler-aspace` — task address-space root association and CR3 activation/switch logic.
- `test-syscall-x86_64-abi` — trap-frame register/argument/result mapping.

These targets do not make the planned unified QTest framework implemented. The framework registration/reporting/filtering architecture above remains a plan.
