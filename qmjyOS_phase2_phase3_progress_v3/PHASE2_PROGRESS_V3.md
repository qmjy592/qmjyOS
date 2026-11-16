# qmjyOS Phase 2 progress — v3

## This tranche

The BIOS bootstrap contained two consecutive CPUID NX capability checks and two EFER.NXE writes. The first sequence did not add a capability beyond the later `RING3_NXE_CPU_CHECK` branch. The later branch also preserves EBX around CPUID and provides both supported and unsupported NX paths. The v3 installer removes only the first exact duplicate block.

The retained sequence has these invariants:

1. Multiboot EAX and EBX are copied into `boot_mb_magic` and `boot_mb_info` before CPUID clobbers general-purpose registers.
2. Extended CPUID leaf `0x80000001` is queried once.
3. EDX bit 20 is tested once.
4. NX-capable CPUs get EFER.LME + EFER.NXE.
5. CPUs without NX still get EFER.LME, without setting NXE.
6. CR0.PG and CR0.WP are enabled after EFER setup.
7. Existing GDT loading and the far jump to physical `0x200000` remain unchanged.

## Validation

- Static source regression test: PASS.
- i386 freestanding assembly compile: PASS.
- Installer first application: PASS.
- Installer second application: PASS (`[ALREADY]`, no duplicate writes).

This is cleanup of a duplicated source sequence, not a fix for a proven boot crash. It does not establish QEMU boot success.

## Overall progress boundary

The package also contains the earlier Phase 2 foundation and MM_HEAP tranches. MM_HEAP remains a bounded static `.bss` allocator and is marked experimental. Boot-info validation, initialization registry and syscall dispatch are still standalone foundations until connected to the real boot and Ring 3 paths. Phase 2 and Phase 3 remain incomplete; outstanding work is listed in `PHASE2_PHASE3_GAP_AUDIT.md`.
