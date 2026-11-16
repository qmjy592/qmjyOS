#!/usr/bin/env python3
"""Static regression checks for the BIOS bootstrap NXE feature path."""
from pathlib import Path
import sys
import re

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "boot/bios/boot.S"


def require(condition: bool, message: str) -> None:
    if not condition:
        print(f"[FAIL] {message}", file=sys.stderr)
        raise SystemExit(1)
    print(f"[PASS] {message}")


text = SOURCE.read_text(encoding="utf-8")
require(len(re.findall(r"(?m)^\s*movl? \$0x80000000, %eax$", text)) == 1,
        "exactly one CPUID extended-leaf detection sequence")
require(text.count("btl $20, %edx") == 1,
        "exactly one CPUID NX feature-bit test")
require(text.count("orl $((1 << 8) | (1 << 11)), %eax") == 1,
        "NX-supported path enables LME and NXE")
require(text.count("orl $(1 << 8), %eax") == 1,
        "NX-unavailable fallback enables LME without NXE")
require("or $0x80010000, %eax" in text,
        "CR0.PG and CR0.WP are enabled after EFER setup")
feature_query = re.search(r"(?m)^\s*movl? \$0x80000000, %eax$", text)
require(feature_query is not None, "extended CPUID query present")
require(text.index("mov %eax, boot_mb_magic") < feature_query.start(),
        "Multiboot EAX saved before CPUID")
require(text.index("mov %ebx, boot_mb_info") < feature_query.start(),
        "Multiboot EBX saved before CPUID")
require("lgdt gdt64_ptr" in text and ".long 0x200000" in text,
        "existing GDT load and long-mode far jump retained")
print("BIOS NXE static regression test: PASS")
