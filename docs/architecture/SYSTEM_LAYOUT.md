# qmjyOS System Directory Layout

## 1. Project Structure

qmjyOS is a self-developed operating system.

The project separates architecture-specific code, generic kernel
services, boot management, drivers, user-space services and tools.

## 2. Top-level Directories

| Directory | Responsibility |
|---|---|
| arch/ | CPU architecture implementations |
| boot/ | QBoot boot manager and handoff protocol |
| config/ | Build and module configuration |
| docs/ | Architecture and development documentation |
| drivers/ | Hardware drivers |
| fs/ | Filesystem implementations |
| include/qmjy/ | Public kernel interfaces |
| kernel/ | Generic kernel core |
| memory/ | Memory management components, where applicable |
| network/ | Networking components |
| package/ | Package management |
| services/ | System services |
| shell/ | Command-line environments |
| tests/ | Host and target tests |
| tools/ | Build and development tools |
| apps/ | Applications |
| desktop/ | Desktop environment |
| installer/ | System installer |
| recovery/ | Recovery environment |

Existing project directories remain authoritative.
This document does not require moving existing source files.

## 3. Architecture Directories

arch/common/
    Architecture-independent contracts and documentation.

arch/x86_64/
    x86_64 CPU, interrupt, paging and context-switch implementations.

arch/aarch64/
    AArch64 exception, MMU, interrupt and context-switch implementations.

## 4. Boot Directories

boot/common/
    Shared QBoot protocol and boot policy.

boot/bios/
    Self-developed BIOS boot implementation.

boot/uefi/
    Self-developed UEFI boot application.

boot/aarch64/
    ARM64 platform boot implementation.

boot/protocols/
    Versioned boot-to-kernel handoff specifications.

## 5. Separation Rules

1. Generic kernel code must not depend on firmware-specific interfaces.
2. Architecture-specific assembly must remain in its architecture tree.
3. Bootloader code must not be confused with kernel initialization.
4. Drivers must use public device interfaces.
5. Filesystem implementations must use common filesystem contracts.
6. Build tools must not silently claim unsupported targets are supported.

## 6. Completion Criteria

A directory is only a structural boundary.

A subsystem is implemented only when source code, integration,
build coverage and relevant tests support that status.
