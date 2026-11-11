# QBoot BIOS

This directory is reserved for the self-developed x86_64 BIOS boot path.

Planned components:

- BIOS entry
- Real-mode initialization
- Protected-mode transition
- Long-mode transition
- Memory discovery
- Kernel image loading
- Boot information construction
- Transfer to the kernel entry point

Implementation requirements:

- Validate the loaded kernel image.
- Validate memory ranges before use.
- Establish a documented CPU state before handoff.
- Pass a valid qm_boot_info_t structure.
- Handle boot failures without silently continuing.

The existing development bootstrap is not yet the complete QBoot BIOS manager.
