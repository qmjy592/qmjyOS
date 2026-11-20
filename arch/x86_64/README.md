# x86_64 Architecture

This directory contains x86_64-specific kernel implementation.

## Responsibilities

- CPU initialization
- GDT and TSS
- IDT and exception entry
- Interrupt controller integration
- CPU feature detection
- Control register management
- x86_64 page-table operations
- Context switching
- Per-CPU state
- Architecture timers

## Rules

- Generic kernel code must not include private architecture headers.
- Assembly interfaces must have documented calling conventions.
- Interrupt entry and exit must preserve the required CPU state.
- Privilege transitions must be explicitly validated.
- Page-table changes must obey architecture-specific invalidation rules.

## Boot

The planned x86_64 boot targets are BIOS and UEFI.

CI covers x86_64 + BIOS and an initial x86_64 + UEFI path. The BIOS bootstrap is not a completed QBoot manager. The UEFI loader/kernel handoff has build and OVMF runtime test coverage, but remains experimental and is not release-ready. AArch64 is not supported yet.
