# Phase 2 boot CPU-feature cleanup

This tranche removes one duplicate NX feature-detection / EFER.NXE sequence
from `boot/bios/boot.S`. It retains the later branch that preserves EBX and
handles NX-supported and NX-unavailable CPUs separately. It also adds a
static check to the existing `test-boot` recipe and documents the behavior.

It does not change the Multiboot handoff, page-table layout, GDT contents,
far jump, CR0 transition, or QBoot disk logic.
