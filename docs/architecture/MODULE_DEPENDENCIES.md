# qmjyOS Module Dependency Reference

> Status: compatibility reference
>
> The machine-readable dependency graph is maintained in
> `config/modules.dep`.
> The module selection configuration is maintained in
> `config/modules.conf`.
> QBuild validates these files and generates the build plan.

This document provides architectural guidance only.
It is not an independent source of build dependencies.

## 1. Architectural layers

1. Boot: QBoot and the boot handoff protocol.
2. Architecture: CPU, registers, interrupts and context switching.
3. Kernel Core: initialization, logging, exceptions and core services.
4. Memory: physical memory, paging and memory allocation.
5. Process and Scheduler: processes, threads and scheduling.
6. IPC: inter-process communication.
7. Drivers: device models and hardware drivers.
8. Storage: block devices, partitions and volumes.
9. Filesystem: VFS and filesystem implementations.
10. Network: network devices and protocol stack.
11. Syscall: user/kernel system-call boundary.
12. Runtime: user-space runtime and program loading.
13. Services: system services.
14. Shell and Desktop: command-line and graphical environments.
15. Applications: system applications.
16. Package, Installer and Recovery: system management tools.
17. Development Tools: build, debug and test infrastructure.

## 2. Dependency principles

- Dependencies must point toward lower-level services and stable interfaces.
- Architecture-specific implementation must not leak into generic kernel interfaces.
- Drivers must not depend on applications.
- Filesystems must use storage interfaces rather than specific controller registers.
- User-space applications must use system-call and user-space library interfaces.
- Circular module dependencies are forbidden.
- Optional modules must not silently become mandatory build dependencies.

## 3. Cross-architecture rules

Public interfaces belong in `include/qmjy/`.
Architecture-independent implementation belongs in appropriate common components.
Architecture-specific implementation belongs in:

- `arch/x86_64/`
- `arch/aarch64/`

Do not introduce new AArch64 implementation under the historical `arch/arm64/`
path.

## 4. Build dependency authority

The following files are authoritative:

- `config/modules.conf`: module selection state
- `config/modules.dep`: module dependency graph
- `config/modules.status`: implementation maturity
- `tools/qbuild/module_plan.py`: source-to-object mapping and validation
- `build/modules.mk`: generated build plan; do not edit manually

When this reference conflicts with the machine-readable configuration,
the machine-readable configuration and QBuild validation take precedence.

## 5. Status

This document is retained as an architectural reference.
It does not replace the current module status model in
`docs/architecture/MODULE_STATUS_MODEL.md`.
