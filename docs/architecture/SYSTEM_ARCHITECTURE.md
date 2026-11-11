# qmjyOS System Architecture

> Status: historical architecture baseline
>
> This document preserves the original high-level architecture direction.
> It is not the authoritative description of the current repository layout,
> module dependencies, implementation status or build capabilities.

## 1. Project direction

qmjyOS is a self-developed operating system targeting x86_64 and AArch64.

The long-term design includes:

- A self-developed QBoot boot manager
- Architecture-separated kernel implementation
- Modular memory, process, scheduler and driver subsystems
- VFS and multiple filesystem implementations
- Storage and network stacks
- User-space runtime, services, shell and desktop
- Package management, installation and recovery
- Unified build, debug and test tools

These are project goals, not claims that every subsystem is implemented.

## 2. Current authoritative documents

Use the following documents for current decisions:

| Subject | Authoritative source |
|---|---|
| Repository directory responsibilities | `docs/architecture/SYSTEM_LAYOUT.md` |
| Public interfaces and contracts | `docs/architecture/INTERFACE_CONTRACTS.md` |
| Module implementation maturity | `docs/architecture/MODULE_STATUS_MODEL.md` |
| Module selection | `config/modules.conf` |
| Module dependencies | `config/modules.dep` |
| Module status | `config/modules.status` |
| Build source mapping | `tools/qbuild/module_plan.py` |
| QBuild usage and supported targets | `tools/qbuild/README.md` |
| QBoot directory layout | `boot/README.md` |
| QBoot handoff protocol | `boot/protocols/README.md` |
| Host-side and boot-image tests | `tests/README.md` |

## 3. Architecture principles

- Generic kernel code must not depend on private architecture structures.
- Architecture-specific code must remain inside its architecture directory.
- Drivers must use kernel and device interfaces.
- Filesystems must use VFS and storage interfaces.
- Applications must not directly access hardware registers.
- Module dependencies must remain acyclic.
- Public interfaces must document ownership, execution context and ABI rules.

## 4. Canonical architecture directories

Current architecture implementation paths:

- `arch/x86_64/`
- `arch/aarch64/`
- `arch/common/`

Use `aarch64` for new AArch64 work.
Do not create new implementation under historical `arm64` paths.

## 5. Boot strategy

The current build target is x86_64 + BIOS.

Planned extensions include:

- x86_64 + UEFI
- AArch64 boot support

QBoot is intended to be developed by qmjyOS itself.
The current development BIOS bootstrap is not a complete QBoot manager.

## 6. Implementation and verification policy

A planned architecture or module must not be described as supported merely
because its directory, interface or design document exists.

Build success, host-side tests, static image checks, emulator boot tests
and hardware runtime tests are distinct evidence levels.

Current implementation maturity and verification evidence must follow
`docs/architecture/MODULE_STATUS_MODEL.md`.
