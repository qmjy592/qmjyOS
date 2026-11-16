#!/usr/bin/env python3
"""Static selector-contract regression checks for both BIOS entry paths."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
qboot = (ROOT / "boot/bios/qboot_stage2.S").read_text(encoding="utf-8")
entry = (ROOT / "arch/x86_64/boot/entry64.S").read_text(encoding="utf-8")
main = (ROOT / "kernel/core/main.c").read_text(encoding="utf-8")
multiboot = (ROOT / "boot/bios/boot.S").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"[FAIL] {message}")
    print(f"[PASS] {message}")


require("ljmpl $0x18, $protected_entry" in qboot,
        "QBoot enters 32-bit protected mode through selector 0x18")
require("ljmp $0x08, $long_mode_entry" in qboot,
        "QBoot enters 64-bit long mode through selector 0x08")
require(re.search(
    r"gdt:\s*\.quad 0\s*\.quad 0x00AF9A000000FFFF.*\n\s*\.quad 0x00CF92000000FFFF.*\n\s*\.quad 0x00CF9A000000FFFF",
    qboot), "QBoot GDT uses 64-bit code at 0x08 and 32-bit code at 0x18")
require("0x00AF9A000000FFFF" in multiboot,
        "Multiboot bootstrap retains 64-bit code at selector 0x08")
require("movw %cs, %di" in entry and "call idt_init_with_selector" in entry,
        "early IDT uses the actual current CS selector")
require("mov $0x08, %edi" not in entry,
        "early IDT no longer assumes a hard-coded loader selector")

arch = main.split("static qm_status_t kernel_init_arch(void)", 1)[1].split(
    "static qm_status_t kernel_init_memory(void)", 1)[0]
require(arch.index("init_gdt_and_tss();") < arch.index("idt_init();"),
        "kernel GDT/TSS is active before replacing the early IDT")

# The capped E820 loop must fail rather than accept a silently truncated map.
require(re.search(r"cmpw \$64, e820_count\s+jae 5f", qboot) is not None,
        "E820 entry-capacity exhaustion reaches the failure path")
require(re.search(r"(?m)^5:\s*\n\s*stc\s*\n\s*ret", qboot) is not None,
        "E820 capacity/API errors return carry to the boot caller")

print("QBoot GDT/early-IDT selector tests: PASS")
