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

The current build target is x86_64 + BIOS.
Its development bootstrap is not a completed QBoot BIOS manager.

The UEFI path remains planned and must not be reported as supported
until its implementation and verification requirements are satisfied.
