# AArch64 Architecture

This directory is reserved for AArch64-specific kernel implementation.

## Responsibilities

- Exception Level initialization
- Exception vector table
- GIC interrupt controller integration
- MMU and translation tables
- System register management
- CPU feature detection
- Context switching
- Per-CPU state
- Architecture timers

## Rules

- Generic kernel code must use public architecture interfaces.
- AArch64 assembly must document register and stack conventions.
- Exception handling must preserve the required execution context.
- MMU changes must follow architecture-specific TLB rules.
- Platform-specific code must remain separate from generic CPU code.

## Boot

The planned AArch64 boot path must pass the common QBoot handoff protocol.

This directory is a structural and implementation plan, not a claim
that AArch64 currently boots.
