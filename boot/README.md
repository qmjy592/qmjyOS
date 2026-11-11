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

- Complete QBoot BIOS manager
- x86_64 UEFI application
- AArch64 boot implementation
- Boot menu and persistent boot entries
- Kernel image validation and loading
- Secure Boot policy and image signature verification

The common handoff contract is documented in protocols/README.md
and declared in include/qmjy/boot.h.

A directory or protocol definition does not prove that a boot path
has been implemented or runtime-verified.
