# QBoot AArch64

This directory is reserved for the self-developed AArch64 boot path.

Planned responsibilities:

- Platform entry
- CPU and exception-level preparation
- Memory discovery
- Kernel image loading
- Device tree or platform description discovery
- Initial translation-table setup
- Common boot information construction
- Transfer to the AArch64 kernel entry

The exact entry state and platform requirements must be specified
for each supported AArch64 platform.

This directory does not indicate current ARM64 boot support.
