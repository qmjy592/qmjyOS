# QBoot UEFI

This directory is reserved for the self-developed UEFI boot application.

Planned components:

- UEFI application entry
- Firmware and CPU environment detection
- Memory map acquisition
- Kernel image loading
- Boot configuration parsing
- Graphics output initialization
- ACPI information discovery
- ExitBootServices handling
- QBoot handoff construction

Requirements:

- Obtain and preserve the final memory map.
- Handle ExitBootServices failures correctly.
- Do not call boot services after successful ExitBootServices.
- Pass the common boot protocol to the kernel.
- Keep firmware-specific code out of the generic kernel.
