# QBoot UEFI

An initial x86_64 UEFI boot application and kernel handoff are implemented and included in CI. This directory is no longer only a placeholder. The current path builds `EFI/BOOT/BOOTX64.EFI`, places the x86_64 UEFI-entry ELF kernel on the ESP, obtains firmware memory information, normalizes it into `qm_boot_info_t`, and invokes `ExitBootServices` with MapKey retry handling.

## Verification

- UEFI PE/COFF application and ELF64 kernel build.
- Static ELF64 `PT_LOAD` layout validation.
- OVMF/QEMU runtime serial checks, including handoff and kernel initialization markers.

This is an initial experimental implementation, not a release-ready UEFI boot manager. Secure Boot, persistent boot-menu entries, full firmware compatibility, runtime-services use, hardened image/signature policy and final hybrid-image packaging remain incomplete.
