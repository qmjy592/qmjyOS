# QBoot Boot Directory Layout

QBoot is the self-developed boot manager of qmjyOS.
GRUB and other third-party bootloaders are not the qmjyOS boot manager.

## Canonical directory layout

- bios/: x86_64 BIOS boot path
- uefi/: UEFI boot application and architecture-specific UEFI code
- aarch64/: AArch64 platform boot path
- common/: firmware-independent QBoot components
- protocols/: QBoot-to-kernel handoff protocol definitions
- qboot/: reserved compatibility placeholder; not an independent implementation
- arm64/: historical compatibility placeholder; use aarch64/ for new work

New implementation must use the canonical directories above.
Do not add new source files to compatibility placeholders.

## Current implementation status

The current x86_64 + BIOS path is a development bootstrap.
It is not yet the complete QBoot BIOS manager.

The following remain planned:

- Complete QBoot BIOS manager (the BIOS disk image is built and QEMU-tested in CI, but the manager is still a development implementation)
- Securely hardened/release-ready UEFI path (an initial EFI loader and kernel handoff are built and OVMF-tested in CI; firmware compatibility and security hardening remain incomplete)
- AArch64 boot implementation
- Boot menu and persistent boot entries
- Kernel image validation and loading
- Secure Boot policy and image signature verification

The common handoff contract is documented in protocols/README.md
and declared in include/qmjy/boot.h.

A directory or protocol definition does not prove that a boot path
has been implemented or runtime-verified.


## UEFI first implementation scope

The `qboot-uefi` target builds `EFI/BOOT/BOOTX64.EFI` and an x86_64 kernel ELF
whose entry is `_start64_uefi`. The linker page-aligns RX/R/RW ELF segments; the
loader stages all PT_LOAD segments as EFI_LOADER_DATA to avoid firmware
write-protection during copying, then the kernel replaces the firmware page
 tables before executing the image. It gathers GOP and
ACPI information, normalizes the UEFI memory
map into `qm_boot_info_t`, and calls ExitBootServices with MapKey retry.

The firmware entry and ELF format can be built independently. This does not
constitute a supported release until `make test-qboot-uefi` passes on OVMF and
BIOS regression tests continue to pass. Secure Boot, runtime services, a full
boot menu, and final hybrid ISO authoring remain separate work.
