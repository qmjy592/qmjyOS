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
