#!/usr/bin/env bash
set -Eeuo pipefail

# Find qmjyOS regardless of whether this script is stored in the repository root,
# tools/repair/, or invoked by an absolute path while the shell is elsewhere.
find_project_root() {
    local start="$1"
    local here
    here="$(CDPATH= cd -- "$start" 2>/dev/null && pwd -P)" || return 1
    while :; do
        if [[ -f "$here/Makefile" && \
              -f "$here/arch/x86_64/idt/idt.c" && \
              -f "$here/boot/bios/qboot_stage2.S" ]]; then
            printf '%s\n' "$here"
            return 0
        fi
        [[ "$here" == "/" ]] && break
        here="$(dirname -- "$here")"
    done
    return 1
}

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
ROOT=""
ROOT="$(find_project_root "$SCRIPT_DIR" 2>/dev/null || true)"
if [[ -z "$ROOT" ]]; then
    ROOT="$(find_project_root "$PWD" 2>/dev/null || true)"
fi
if [[ -z "$ROOT" ]]; then
    echo "[FAIL] Cannot locate qmjyOS project root." >&2
    echo "       Expected Makefile, arch/x86_64/idt/idt.c and boot/bios/qboot_stage2.S." >&2
    exit 1
fi
cd "$ROOT"

printf '[PHASE 13] Project root: %s\n' "$ROOT"
printf '[PHASE 13] Verifying all source anchors before writing any file.\n'

python3 - <<'PY'
from pathlib import Path
from datetime import datetime
import os
import re
import shutil
import stat
import tempfile
import subprocess
import sys

ROOT = Path.cwd()
TARGETS = [
    "boot/bios/qboot_stage2.S",
    "arch/x86_64/idt/idt.h",
    "arch/x86_64/idt/idt.c",
    "arch/x86_64/boot/entry64.S",
    "arch/x86_64/interrupts/excep.S",
    "arch/x86_64/interrupts/exception.c",
    "arch/x86_64/gdt/gdt_load.S",
    "drivers/serial/serial.h",
    "drivers/serial/serial.c",
    "kernel/core/main.c",
    "tools/qboot/test_qfs_ide.sh",
    "tools/qboot/test_bios_boot.sh",
    "tools/qboot/build_bios_image.sh",
    "tools/qboot/build_qfs_ide_images.sh",
]


def stop(message):
    raise SystemExit("[FAIL] " + message + "; no source files were changed.")


original_bytes = {}
original = {}
updated = {}
for name in TARGETS:
    path = ROOT / name
    if not path.is_file():
        stop(f"Missing required source: {name}")
    data = path.read_bytes()
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError:
        stop(f"Source is not UTF-8: {name}")
    original_bytes[name] = data
    original[name] = text
    updated[name] = text


def replace_once(name, old, new, description):
    count = updated[name].count(old)
    if count != 1:
        stop(f"{name}: {description} anchor count is {count}, expected exactly 1")
    updated[name] = updated[name].replace(old, new, 1)


def replace_function(source, signature, replacement, file_name):
    start = source.find(signature)
    if start < 0 or source.find(signature, start + 1) >= 0:
        stop(f"{file_name}: cannot locate one unique function signature: {signature}")
    brace = source.find("{", start)
    if brace < 0:
        stop(f"{file_name}: function opening brace missing: {signature}")

    depth = 0
    i = brace
    state = "code"
    escaped = False
    while i < len(source):
        ch = source[i]
        nxt = source[i + 1] if i + 1 < len(source) else ""
        if state == "line_comment":
            if ch == "\n":
                state = "code"
        elif state == "block_comment":
            if ch == "*" and nxt == "/":
                state = "code"
                i += 1
        elif state == "string":
            if escaped:
                escaped = False
            elif ch == "\\":
                escaped = True
            elif ch == '"':
                state = "code"
        elif state == "char":
            if escaped:
                escaped = False
            elif ch == "\\":
                escaped = True
            elif ch == "'":
                state = "code"
        else:
            if ch == "/" and nxt == "/":
                state = "line_comment"
                i += 1
            elif ch == "/" and nxt == "*":
                state = "block_comment"
                i += 1
            elif ch == '"':
                state = "string"
            elif ch == "'":
                state = "char"
            elif ch == "{":
                depth += 1
            elif ch == "}":
                depth -= 1
                if depth == 0:
                    return source[:start] + replacement + source[i + 1:]
        i += 1
    stop(f"{file_name}: unbalanced braces in function: {signature}")


# ---------------------------------------------------------------------------
# B01: QBoot's per-image layout include must resolve from the image's BUILD_DIR.
# The user's local tree may already contain this exact correction.
# ---------------------------------------------------------------------------
old_include = '.include "build/qboot/kernel_layout.inc"'
new_include = '.include "kernel_layout.inc"'
qboot_text = updated["boot/bios/qboot_stage2.S"]
if qboot_text.count(old_include) == 1 and qboot_text.count(new_include) == 0:
    updated["boot/bios/qboot_stage2.S"] = qboot_text.replace(old_include, new_include, 1)
elif qboot_text.count(new_include) == 1 and qboot_text.count(old_include) == 0:
    print("[ALREADY] QBoot stage2 uses the per-build kernel_layout.inc")
else:
    stop("boot/bios/qboot_stage2.S: expected exactly one old or corrected layout include")

builder = updated["tools/qboot/build_bios_image.sh"]
if '-I "$BUILD_DIR"' not in builder:
    stop('build_bios_image.sh no longer passes -I "$BUILD_DIR" to stage2 assembly')
stage2 = updated["boot/bios/qboot_stage2.S"]
expected_stage2_gdt = (
    "gdt:\n"
    "    .quad 0\n"
    "    .quad 0x00CF9A000000FFFF\n"
    "    .quad 0x00CF92000000FFFF\n"
    "    .quad 0x00AF9A000000FFFF\n"
)
if expected_stage2_gdt not in stage2:
    stop("QBoot Stage2 GDT no longer maps selector 0x18 to a 64-bit code segment")
qfs_builder = updated["tools/qboot/build_qfs_ide_images.sh"]
if 'QBOOT_BUILD_DIR="$output_dir"' not in qfs_builder:
    stop("QFS builder no longer supplies its independent output directory to QBoot")

# ---------------------------------------------------------------------------
# B02/B03: Two IDT tables and a safe selector transition.
# Stage2 uses CS=0x18. The new GDT temporarily keeps 0x18 as a code alias
# until the kernel IDT (selector 0x08) has been loaded. Only then does main.c
# turn GDT slot 3 back into the Ring 3 data descriptor.
# ---------------------------------------------------------------------------
replace_once(
    "arch/x86_64/idt/idt.h",
    "void idt_init(void);\n",
    "void idt_init(void);\n"
    "void idt_init_with_selector(uint16_t selector);\n"
    "void idt_prepare_kernel(void);\n",
    "IDT API declarations",
)

replace_once(
    "arch/x86_64/idt/idt.c",
    "static idt_entry_t idt[256];\nstatic idt_ptr_t idt_ptr;\n",
    "static idt_entry_t idt_stage2[256];\n"
    "static idt_entry_t idt_kernel[256];\n"
    "static idt_entry_t *idt_build_target;\n"
    "static uint16_t idt_code_selector;\n"
    "static idt_ptr_t idt_stage2_ptr;\n"
    "idt_ptr_t idt_kernel_ptr;\n",
    "IDT storage declarations",
)

old_gate = '''static void set_idt_gate(unsigned int n, uint64_t handler) {
    idt[n].offset_low = (uint16_t)(handler & 0xFFFF);
    idt[n].selector = 0x08;
    idt[n].ist = 0;
    idt[n].type_attr = 0x8E;
    idt[n].offset_mid = (uint16_t)((handler >> 16) & 0xFFFF);
    idt[n].offset_high = (uint32_t)(handler >> 32);
    idt[n].zero = 0;
}'''
new_gate = '''static void set_idt_gate(unsigned int n, uint64_t handler) {
    idt_build_target[n].offset_low = (uint16_t)(handler & 0xFFFF);
    idt_build_target[n].selector = idt_code_selector;
    idt_build_target[n].ist = 0;
    idt_build_target[n].type_attr = 0x8E;
    idt_build_target[n].offset_mid = (uint16_t)((handler >> 16) & 0xFFFF);
    idt_build_target[n].offset_high = (uint32_t)(handler >> 32);
    idt_build_target[n].zero = 0;
}'''
replace_once("arch/x86_64/idt/idt.c", old_gate, new_gate, "IDT gate writer")

# Confirm that all 256 existing vector-to-stub assignments are intact before
# changing only the table-selection mechanism.
idt_text = updated["arch/x86_64/idt/idt.c"]
vector_calls = re.findall(
    r"^\s*set_idt_gate\((\d+), \(uint64_t\)([A-Za-z0-9_]+)\);\s*$",
    idt_text,
    re.MULTILINE,
)
if (len(vector_calls) != 257 or
        [int(item[0]) for item in vector_calls[:256]] != list(range(256)) or
        vector_calls[-1] != ("128", "syscall_entry")):
    stop("idt.c: expected vectors 0..255 plus the final Ring 3 syscall-gate override")

replace_once(
    "arch/x86_64/idt/idt.c",
    "void idt_init(void) {\n",
    "static void idt_build(idt_entry_t *target, idt_ptr_t *ptr, uint16_t selector) {\n"
    "    idt_build_target = target;\n"
    "    idt_code_selector = selector;\n",
    "IDT builder function entry",
)
replace_once(
    "arch/x86_64/idt/idt.c",
    "    idt[128].type_attr = 0xEE;\n"
    "    idt_ptr.limit = (uint16_t)(sizeof(idt) - 1);\n"
    "    idt_ptr.base = (uint64_t)idt;\n"
    '    __asm__ volatile("lidt %0" :: "m"(idt_ptr) : "memory");\n'
    "}",
    "    idt_build_target[128].type_attr = 0xEE;\n"
    "    ptr->limit = (uint16_t)(sizeof(idt_entry_t) * 256 - 1);\n"
    "    ptr->base = (uint64_t)target;\n"
    "}\n"
    "\n"
    "void idt_init_with_selector(uint16_t selector) {\n"
    "    if (selector == 0x18) {\n"
    "        idt_build(idt_stage2, &idt_stage2_ptr, 0x18);\n"
    '        __asm__ volatile("lidt %0" :: "m"(idt_stage2_ptr) : "memory");\n'
    "        return;\n"
    "    }\n"
    "    if (selector != 0x08)\n"
    "        return;\n"
    "    idt_build(idt_kernel, &idt_kernel_ptr, 0x08);\n"
    '    __asm__ volatile("lidt %0" :: "m"(idt_kernel_ptr) : "memory");\n'
    "}\n"
    "\n"
    "void idt_prepare_kernel(void) {\n"
    "    idt_build(idt_kernel, &idt_kernel_ptr, 0x08);\n"
    "}\n"
    "\n"
    "void idt_init(void) {\n"
    "    idt_init_with_selector(0x08);\n"
    "}",
    "IDT table finalization and API wrappers",
)

# Early IDT is installed after BSS has been cleared, while Stage2's 0x18
# 64-bit code descriptor is still active.
replace_once(
    "arch/x86_64/boot/entry64.S",
    ".extern __bss_start\n.extern __bss_end\n",
    ".extern __bss_start\n.extern __bss_end\n"
    ".extern idt_init_with_selector\n",
    "entry64 external declarations",
)
replace_once(
    "arch/x86_64/boot/entry64.S",
    "    # Restore arguments and call main(magic, info_addr).\n"
    "    mov %r12d, %edi\n",
    "    # Install an early IDT using Stage2's 64-bit code selector (0x18).\n"
    "    mov $0x18, %edi\n"
    "    call idt_init_with_selector\n"
    "\n"
    "    # Restore arguments and call main(magic, info_addr).\n"
    "    mov %r12d, %edi\n",
    "early IDT call before main",
)

# The saved exception frame is: vector, error, RIP, CS, RFLAGS after 15 GPRs.
replace_once(
    "arch/x86_64/interrupts/excep.S",
    "    mov 136(%rsp), %rdx\n"
    "    mov %rsp, %r12\n",
    "    mov 136(%rsp), %rdx\n"
    "    mov 144(%rsp), %rcx\n"
    "    mov 152(%rsp), %r8\n"
    "    mov %rsp, %r12\n",
    "exception frame argument setup",
)

# The transition alias at GDT selector 0x18 keeps the early Stage2 IDT valid
# from LGDT until LIDT. It is changed to user-data only after LIDT succeeds.
replace_once(
    "arch/x86_64/gdt/gdt_load.S",
    ".global gdt_load_and_reload_cs\n.type gdt_load_and_reload_cs, @function\n",
    ".global gdt_load_and_reload_cs\n.type gdt_load_and_reload_cs, @function\n"
    ".extern idt_prepare_kernel\n.extern idt_kernel_ptr\n",
    "GDT loader extern declarations",
)
replace_once(
    "arch/x86_64/gdt/gdt_load.S",
    "gdt_load_and_reload_cs:\n"
    "    lgdt (%rdi)\n",
    "gdt_load_and_reload_cs:\n"
    "    # Build the 0x08 kernel IDT while the old 0x18 Stage2 IDT is active.\n"
    "    # Entry RSP is 8 mod 16; push makes it 16-byte aligned before call.\n"
    "    pushq %rdi\n"
    "    call idt_prepare_kernel\n"
    "    popq %rdi\n"
    "    lgdt (%rdi)\n"
    "    # GDT slot 0x18 remains a code alias until this IDT is active.\n"
    "    lidt idt_kernel_ptr(%rip)\n",
    "GDT/IDT transition sequence",
)

# Serial can be queried safely by a panic handler before COM1 is initialized.
replace_once(
    "drivers/serial/serial.h",
    "void serial_puts(const char *s);\n",
    "void serial_puts(const char *s);\nint serial_is_initialized(void);\n",
    "serial initialization-state API",
)
replace_once(
    "drivers/serial/serial.c",
    "#define COM1 0x3F8\n",
    "#define COM1 0x3F8\nstatic int serial_initialized;\n",
    "serial state storage",
)
replace_once(
    "drivers/serial/serial.c",
    "void serial_init(void) {\n"
    "    outb(COM1 + 1, 0x00);\n",
    "void serial_init(void) {\n"
    "    serial_initialized = 0;\n"
    "    outb(COM1 + 1, 0x00);\n",
    "serial init start state",
)
replace_once(
    "drivers/serial/serial.c",
    "    outb(COM1 + 4, 0x0B);\n}\n",
    "    outb(COM1 + 4, 0x0B);\n"
    "    serial_initialized = 1;\n"
    "}\n"
    "\n"
    "int serial_is_initialized(void) {\n"
    "    return serial_initialized;\n"
    "}\n",
    "serial init completion state",
)

# Keep the existing exception_messages[] and print_hex() exports intact.
exception_file = "arch/x86_64/interrupts/exception.c"
exception_source = updated[exception_file]
old_exception_signature = (
    "void exception_handler(uint64_t int_no, uint64_t err_code, uint64_t rip)"
)
if exception_source.count(old_exception_signature) != 1:
    stop("exception.c: original three-argument exception_handler signature not found exactly once")

panic_helpers = r'''static void panic_putc(char c)
{
    __asm__ volatile(
        "outb %0, %1"
        :
        : "a"((uint8_t)c), "Nd"((uint16_t)0xE9)
    );
    if (serial_is_initialized())
        serial_putc(c);
}

static void panic_puts(const char *s)
{
    while (*s) {
        if (*s == '\n')
            panic_putc('\r');
        panic_putc(*s++);
    }
}

static void panic_hex(uint64_t value)
{
    static const char digits[] = "0123456789ABCDEF";
    unsigned int i;
    panic_putc('0');
    panic_putc('x');
    for (i = 0; i < 16; ++i)
        panic_putc(digits[(value >> (60 - i * 4)) & 0x0F]);
}

'''
if "static void panic_putc(char c)" in exception_source:
    stop("exception.c: panic output helpers already exist; refusing mixed-state rewrite")
exception_source = exception_source.replace(
    old_exception_signature,
    panic_helpers + "void exception_handler(uint64_t int_no, uint64_t err_code, "
    "uint64_t rip, uint64_t cs, uint64_t rflags)",
    1,
)
new_exception_function = r'''void exception_handler(uint64_t int_no, uint64_t err_code,
                       uint64_t rip, uint64_t cs, uint64_t rflags)
{
    uint64_t cr2 = 0;

    panic_puts("\n\n!!! KERNEL PANIC !!!\n");
    panic_puts("Exception: ");
    if (int_no < 32)
        panic_puts(exception_messages[int_no]);
    else
        panic_puts("Unknown");

    panic_puts("\nVector: ");
    panic_hex(int_no);
    panic_puts("\nError Code: ");
    panic_hex(err_code);
    panic_puts("\nRIP: ");
    panic_hex(rip);
    panic_puts("\nCS: ");
    panic_hex(cs);
    panic_puts("\nRFLAGS: ");
    panic_hex(rflags);

    if (int_no == 14) {
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        panic_puts("\nCR2: ");
        panic_hex(cr2);
    }

    panic_puts("\n\nSystem Halted.\n");
    for (;;) {
        __asm__ volatile("cli; hlt" ::: "memory");
    }
}'''
exception_source = replace_function(
    exception_source,
    "void exception_handler(uint64_t int_no, uint64_t err_code, uint64_t rip, uint64_t cs, uint64_t rflags)",
    new_exception_function,
    exception_file,
)
updated[exception_file] = exception_source

# GDT slot 0x18 is temporarily a second kernel-code descriptor. This is what
# makes the old Stage2 IDT valid across the LGDT/LIDT instruction boundary.
replace_once(
    "kernel/core/main.c",
    "    // User Data: 0x18 (DPL=3)\n"
    "    my_gdt[3] = 0x00CFF2000000FFFF;\n",
    "    // Transition alias: keep Stage2 IDT selector 0x18 valid during LGDT/LIDT.\n"
    "    my_gdt[3] = 0x00AF9A000000FFFF;\n",
    "temporary GDT code alias at selector 0x18",
)
replace_once(
    "kernel/core/main.c",
    "    gdt_load_and_reload_cs(&gdtr);\n"
    '    __asm__ volatile("ltr %0" :: "r"((uint16_t)0x28)); // TSS 选择子\n',
    "    gdt_load_and_reload_cs(&gdtr);\n"
    "    // Kernel IDT selector 0x08 is active now; restore Ring 3 data at 0x18.\n"
    "    my_gdt[3] = 0x00CFF2000000FFFF;\n"
    '    __asm__ volatile("ltr %0" :: "r"((uint16_t)0x28)); // TSS 选择子\n',
    "restore Ring 3 data descriptor after IDT switch",
)
replace_once(
    "kernel/core/main.c",
    "    pic_remap();\n"
    "    idt_init();\n"
    "    timer_init(100);\n"
    "\n"
    "    init_gdt_and_tss();\n",
    "    // entry64 installed the Stage2 IDT; GDT loader switches to kernel IDT.\n"
    "    init_gdt_and_tss();\n"
    "    pic_remap();\n"
    "    timer_init(100);\n",
    "main GDT/IDT/PIC/Timer initialization order",
)

# B05: preserve QFS IDE runtime logs and disk image on both pass and failure.
qfs_test = "tools/qboot/test_qfs_ide.sh"
replace_once(
    qfs_test,
    'cleanup() {\n'
    '    rm -rf "$TEST_DIR"\n'
    '}\n'
    'trap cleanup EXIT\n',
    "# Keep runtime logs and the test disk for post-run diagnosis.\n",
    "QFS test runtime cleanup trap",
)
replace_once(
    qfs_test,
    'fail() {\n'
    '    echo "[QFS IDE TEST FAIL] $*" >&2\n',
    'fail() {\n'
    '    echo "[QFS IDE TEST FAIL] $*" >&2\n'
    '    echo "[QFS IDE TEST] Runtime evidence retained: $RUN_ROOT" >&2\n',
    "QFS failure evidence path",
)
replace_once(
    qfs_test,
    'echo "[QFS IDE TEST PASS] Complete QFS IDE persistence test passed."\n',
    'echo "[QFS IDE TEST PASS] Complete QFS IDE persistence test passed."\n'
    'echo "[QFS IDE TEST] Runtime evidence retained: $RUN_ROOT"\n',
    "QFS success evidence path",
)

# BIOS boot test logs move from /tmp into a unique build directory and remain
# available after the test, which is also compatible with Termux storage rules.
bios_test = "tools/qboot/test_bios_boot.sh"
old_bios_log_setup = '''IMAGE="build/qboot/qboot-bios.img"
SERIAL_LOG="${TMPDIR:-/tmp}/qmjyos-qboot-serial-$$.log"
DEBUG_LOG="${TMPDIR:-/tmp}/qmjyos-qboot-debug-$$.log"
TIMEOUT_SECONDS=20

cleanup() {
    rm -f "$SERIAL_LOG" "$DEBUG_LOG"
}
trap cleanup EXIT
'''
new_bios_log_setup = '''IMAGE="build/qboot/qboot-bios.img"
RUN_ID="${QBOOT_TEST_RUN_ID:-$(date +%Y%m%d-%H%M%S)}"
RUN_DIR="build/qboot/test-runs/$RUN_ID"
if [ -e "$RUN_DIR" ]; then
    echo "[QBOOT TEST FAIL] Evidence directory already exists: $RUN_DIR" >&2
    exit 1
fi
mkdir -p "$RUN_DIR"
SERIAL_LOG="$RUN_DIR/serial.log"
DEBUG_LOG="$RUN_DIR/debug.log"
TIMEOUT_SECONDS=20
'''
replace_once(bios_test, old_bios_log_setup, new_bios_log_setup, "BIOS test temporary log setup")
replace_once(
    bios_test,
    'fail() {\n'
    '    echo "[QBOOT TEST FAIL] $*" >&2\n',
    'fail() {\n'
    '    echo "[QBOOT TEST FAIL] $*" >&2\n'
    '    echo "[QBOOT TEST] Evidence retained: $RUN_DIR" >&2\n',
    "BIOS test failure evidence path",
)
replace_once(
    bios_test,
    'echo "[QBOOT TEST PASS] BIOS disk boot and kernel runtime verified."\n',
    'echo "[QBOOT TEST] Evidence retained: $RUN_DIR"\n'
    'echo "[QBOOT TEST PASS] BIOS disk boot and kernel runtime verified."\n',
    "BIOS test success evidence path",
)

# ---------------------------------------------------------------------------
# Whole-patch static invariants: all checked before backup or source writes.
# ---------------------------------------------------------------------------
checks = {
    "arch/x86_64/idt/idt.h": [
        "void idt_init_with_selector(uint16_t selector);",
        "void idt_prepare_kernel(void);",
    ],
    "arch/x86_64/idt/idt.c": [
        "static idt_entry_t idt_stage2[256];",
        "static idt_entry_t idt_kernel[256];",
        "idt_ptr_t idt_kernel_ptr;",
        "idt_build_target[n].selector = idt_code_selector;",
        "idt_build(idt_stage2, &idt_stage2_ptr, 0x18);",
        "idt_build(idt_kernel, &idt_kernel_ptr, 0x08);",
        "void idt_prepare_kernel(void)",
        'set_idt_gate(255, (uint64_t)isr_unexpected_255);',
    ],
    "arch/x86_64/boot/entry64.S": [
        ".extern idt_init_with_selector",
        "rep stosb",
        "mov $0x18, %edi",
        "call idt_init_with_selector",
    ],
    "arch/x86_64/interrupts/excep.S": [
        "mov 120(%rsp), %rdi",
        "mov 128(%rsp), %rsi",
        "mov 136(%rsp), %rdx",
        "mov 144(%rsp), %rcx",
        "mov 152(%rsp), %r8",
        "call exception_handler",
    ],
    "arch/x86_64/interrupts/exception.c": [
        "uint64_t rip, uint64_t cs, uint64_t rflags)",
        "static void panic_putc(char c)",
        'panic_puts("\\nCS: ");',
        'panic_puts("\\nRFLAGS: ");',
        'panic_puts("\\nCR2: ");',
    ],
    "arch/x86_64/gdt/gdt_load.S": [
        "call idt_prepare_kernel",
        "lgdt (%rdi)",
        "lidt idt_kernel_ptr(%rip)",
        "lretq",
    ],
    "kernel/core/main.c": [
        "my_gdt[3] = 0x00AF9A000000FFFF;",
        "my_gdt[3] = 0x00CFF2000000FFFF;",
        "init_gdt_and_tss();\n    pic_remap();\n    timer_init(100);",
    "task_init();",
    "task_start();",
        "ring3_prepare_and_run()",
    ],
    "drivers/serial/serial.c": [
        "static int serial_initialized;",
        "serial_initialized = 1;",
        "int serial_is_initialized(void)",
    ],
    "drivers/serial/serial.h": ["int serial_is_initialized(void);"],
    "tools/qboot/test_qfs_ide.sh": [
        "Runtime evidence retained: $RUN_ROOT",
        "# Keep runtime logs and the test disk",
    ],
    "tools/qboot/test_bios_boot.sh": [
        'RUN_DIR="build/qboot/test-runs/$RUN_ID"',
        'SERIAL_LOG="$RUN_DIR/serial.log"',
        'DEBUG_LOG="$RUN_DIR/debug.log"',
        "Evidence retained: $RUN_DIR",
    ],
    "boot/bios/qboot_stage2.S": [new_include],
}
for name, needles in checks.items():
    for needle in needles:
        if needle not in updated[name]:
            stop(f"post-patch invariant missing in {name}: {needle!r}")

# Validate the CPU-error-code stubs and the exception frame offsets.
asm = updated["arch/x86_64/interrupts/excep.S"]
for vector in (8, 10, 11, 12, 13, 14, 17, 21, 29, 30):
    if f"ISR_ERR {vector}" not in asm:
        stop(f"excep.S: CPU error-code vector {vector} is not marked ISR_ERR")
if "ISR_ERR 21" not in asm or "ISR_ERR 29" not in asm or "ISR_ERR 30" not in asm:
    stop("excep.S: modern architectural error-code exception stubs are incomplete")
if "ISR_NOERR 20" not in asm or "ISR_NOERR 28" not in asm:
    stop("excep.S: #VE/#HV must retain their no-error-code stack layout")

# The GDT slot 0x18 must stay a code alias until after the new IDT is loaded.
gdt_asm = updated["arch/x86_64/gdt/gdt_load.S"]
if not (gdt_asm.find("lgdt (%rdi)") < gdt_asm.find("lidt idt_kernel_ptr(%rip)") < gdt_asm.find("lretq")):
    stop("GDT loader instruction order must be LGDT, LIDT, then far CS reload")
main = updated["kernel/core/main.c"]
if main.find("gdt_load_and_reload_cs(&gdtr);") > main.find("my_gdt[3] = 0x00CFF2000000FFFF;", main.find("gdt_load_and_reload_cs(&gdtr);")):
    stop("GDT transition alias is not restored after kernel IDT activation")

if "trap cleanup EXIT" in updated[qfs_test] or "rm -rf \"$TEST_DIR\"" in updated[qfs_test]:
    stop("QFS IDE runtime cleanup still deletes diagnostic evidence")
if "/tmp" in updated[bios_test] or "trap cleanup EXIT" in updated[bios_test]:
    stop("BIOS test still uses /tmp or deletes diagnostic evidence")

# Ensure no source changed while preflight was running.
for name in TARGETS:
    if (ROOT / name).read_bytes() != original_bytes[name]:
        stop(f"source changed during preflight: {name}")

changed = [name for name in TARGETS if updated[name] != original[name]]
if not changed:
    print("[ALREADY] Phase 13 changes are already present; no files modified.")
    raise SystemExit(0)

# Backups are created only after every source anchor and cross-file invariant
# has passed. The unique timestamp directory is never reused or overwritten.
backup_root = ROOT / "build/repair-backups/phase13-integrated"
backup_root.mkdir(parents=True, exist_ok=True)
backup_dir = backup_root / datetime.now().strftime("%Y%m%d-%H%M%S-%f")
try:
    backup_dir.mkdir(parents=False, exist_ok=False)
except FileExistsError:
    stop(f"backup directory already exists: {backup_dir.relative_to(ROOT)}")

for name in changed:
    destination = backup_dir / (name + ".before-phase13")
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(ROOT / name, destination)
    if destination.read_bytes() != original_bytes[name]:
        stop(f"backup verification failed for {name}")
    print(f"[BACKUP] {destination.relative_to(ROOT)}")

# Stage every changed file beside its destination so os.replace remains atomic
# on the same filesystem. No /tmp files are used.
staged = {}
committed = []

def write_stage(target: Path, data: bytes, mode: int) -> Path:
    fd, temp_name = tempfile.mkstemp(prefix="." + target.name + ".phase13.", dir=str(target.parent))
    temp_path = Path(temp_name)
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.chmod(temp_path, mode)
        return temp_path
    except Exception:
        try:
            os.close(fd)
        except OSError:
            pass
        temp_path.unlink(missing_ok=True)
        raise

try:
    for name in changed:
        target = ROOT / name
        mode = stat.S_IMODE(target.stat().st_mode)
        staged[name] = write_stage(target, updated[name].encode("utf-8"), mode)

    # Parse the exact staged shell-test files before changing source paths.
    for shell_name in ("tools/qboot/test_qfs_ide.sh", "tools/qboot/test_bios_boot.sh"):
        result = subprocess.run(
            ["bash", "-n", str(staged[shell_name])],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        if result.returncode != 0:
            raise RuntimeError(f"bash -n failed for staged {shell_name}: {result.stderr.strip()}")
        print(f"[PASS] staged Bash syntax: {shell_name}")

    # Recheck originals after all temporary files have been staged.
    for name in changed:
        if (ROOT / name).read_bytes() != original_bytes[name]:
            raise RuntimeError(f"source changed before commit: {name}")

    for name in changed:
        os.replace(staged[name], ROOT / name)
        committed.append(name)
        print(f"[UPDATED] {name}")

    for name in changed:
        if (ROOT / name).read_bytes() != updated[name].encode("utf-8"):
            raise RuntimeError(f"post-write verification failed: {name}")

except Exception as error:
    print(f"[ROLLBACK] {error}", file=sys.stderr)
    for name in reversed(committed):
        target = ROOT / name
        rollback = write_stage(
            target,
            original_bytes[name],
            stat.S_IMODE(target.stat().st_mode),
        )
        os.replace(rollback, target)
        print(f"[RESTORED] {name}", file=sys.stderr)
    raise SystemExit("[FAIL] Write/verification failed; committed source files were rolled back.")
finally:
    for temp in staged.values():
        temp.unlink(missing_ok=True)

print("[PHASE 13 PASS] All source anchors and cross-file invariants passed.")
print(f"[PHASE 13 BACKUP] {backup_dir.relative_to(ROOT)}")
print("[PHASE 13 SCOPE] Early exceptions, GDT/IDT transition, serial-safe panic output,")
print("                QBoot image-layout include and persistent QBoot/QFS test evidence.")
print("[PHASE 13 NOTE] QFS crash-transaction/replay recovery is deliberately not changed here.")
PY

printf '\n[PHASE 13] Checking modified shell-test syntax.\n'
bash -n tools/qboot/test_qfs_ide.sh
bash -n tools/qboot/test_bios_boot.sh
printf '[PHASE 13 PASS] Bash syntax checks passed.\n'
printf '[PHASE 13 NOTE] C/ASM compilation and QEMU execution are not claimed by this repair script.\n'
