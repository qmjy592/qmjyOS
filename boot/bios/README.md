# QBoot BIOS

The current x86_64 BIOS development path contains a Multiboot bootstrap and a separate QBoot disk-image build/test path. QBoot Stage 1 and Stage 2 image-size checks are part of the static build validation; the BIOS disk runtime test is exercised under QEMU in CI.

## Implemented foundations

- Multiboot-compatible development bootstrap into the x86_64 kernel.
- QBoot BIOS Stage 1 (512-byte) and Stage 2 (8192-byte) disk-image components.
- Kernel-sector sizing and LBA/image assembly checks.
- Debugcon and serial markers for boot stages and kernel handoff.
- Memory/boot handoff foundations and NX feature setup in the BIOS path.

## Remaining scope

- Full boot manager and user-facing menu.
- Robust boot-entry persistence and recovery.
- Release-grade image validation, compatibility and security policy.
- Secure boot/signature policy and final image packaging.

The existing development bootstrap and QBoot disk path are not yet a completed release boot manager.
