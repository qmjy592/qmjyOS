# BIOS bootstrap CPU feature setup

The BIOS development bootstrap enters 32-bit protected mode and prepares the
long-mode transition. Before setting CR0.PG, it must set CR4.PAE and EFER.LME.
EFER.NXE is conditional: CPUID extended leaf `0x80000001`, EDX bit 20 must
report NX support before EFER.NXE (bit 11) is set. If NX is unavailable, the
bootstrap still enables long mode but does not set NXE. The paging manager's
runtime checks must continue to reject NX mappings unless both CPU support and
EFER.NXE are present.

The canonical sequence in `boot/bios/boot.S` performs this detection once,
preserves EBX around CPUID (EBX holds the Multiboot info pointer until it has
been saved), sets EFER.LME unconditionally, conditionally sets EFER.NXE, and
then enables CR0.PG and CR0.WP. The static regression test is
`tests/boot/test_bios_nxe.py` and is included in `make test-boot`.

This is a source-level and assembly-compilation check. It is not a substitute
for running the boot image in QEMU or on hardware.
