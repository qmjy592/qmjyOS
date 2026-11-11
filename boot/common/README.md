# QBoot Common Components

This directory contains architecture-independent QBoot components.

Planned responsibilities:

- Boot entry selection
- Boot configuration parsing
- Boot menu policy
- Boot entry management
- Kernel image metadata
- Handoff structure preparation
- Boot error reporting
- Protocol compatibility checks

Architecture-specific firmware operations must be implemented
inside the corresponding platform directory.

QBoot must be developed by qmjyOS itself.
GRUB and other third-party bootloaders are not the project boot manager.
