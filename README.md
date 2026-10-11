
# qmjyOS

An experimental x86_64 operating system project.

qmjyOS is a hobby operating system developed in C and x86 assembly.
The project focuses on understanding kernel architecture, interrupt
handling, memory management, privilege separation, and secure system
design through small, testable development steps.

## Current Status

This project is under active development. It is not production-ready.

Current development areas include:

- 32-bit boot handoff to a 64-bit kernel.
- 64-bit kernel entry and linker layout.
- GDT and TSS initialization.
- IDT and exception-handler infrastructure.
- PIC interrupt handling.
- PIT timer initialization and tick counting.
- Early kernel debugging through serial output.

The current boot test has shown the kernel startup messages and timer
interrupt activity in QEMU.

The following features are not yet considered complete or verified:

- 4 KiB page-table management and memory isolation.
- User-mode (Ring 3) execution.
- System-call entry and validation.
- Fully verified cooperative task scheduling.
- Preemptive multitasking.
- Production-grade security and reliability.

## Build Environment

The current Makefile expects these tools:

- Clang
- LLD (`ld.lld`)
- LLVM objcopy (`llvm-objcopy`)
- Make

QEMU is used for runtime testing.

Tool names and package availability depend on the host environment.
Termux may require additional packages or a compatible QEMU setup.

## Build

From the project root:

```sh
make clean
make
```

## Run in QEMU

If `qemu-system-x86_64` is installed and the current boot image is
compatible with the selected QEMU configuration:

```sh
make run
```

Always inspect build output and runtime serial output. A successful
link does not prove that the kernel is correct or secure.

## Development Principles

- Prefer small, reviewable changes.
- Keep a known-good boot checkpoint.
- Preserve recovery copies outside version control.
- Test changes in QEMU before advancing to riskier stages.
- Do not enable Ring 3 before page permissions and kernel mappings
  have been validated.
- Treat compiler success as necessary but insufficient evidence.
- Document limitations and test results honestly.

## Security Status

qmjyOS is experimental software. It has not undergone a comprehensive
security audit and must not be used to protect sensitive data or as a
production operating system.

## License

No license has been selected yet. All rights are reserved by default
until a license is added. Do not assume the code is licensed for
redistribution or reuse.
EOF

echo "===== README created ====="
sed -n '1,200p' README.md
