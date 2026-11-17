#!/usr/bin/env python3
"""QMjYOS UEFI integrated repair patcher.

Applies only source-backed, exact-anchor changes to the user's existing tree:
  1. Split memory-map growth and ExitBootServices attempt counters.
  2. Validate descriptor-stride divisibility before consuming the final map.
  3. Publish the exact UEFI bootstrap page-table allocation size in boot.h.
  4. Validate and reserve the entire 68-page active bootstrap page-table block.
  5. Install an emergency IDT before the UEFI entry switches CR3.

The patcher creates timestamped backups under build/uefi-security-audit/backups.
It never invokes git, make, QEMU, or external tests. It is transactional:
all target transforms are prepared and validated before any source file is written.
"""
from __future__ import annotations

import argparse
import difflib
import os
import re
import shutil
import sys
from datetime import datetime
from pathlib import Path


class PatchError(RuntimeError):
    pass


EXIT_BLOCK = r'''    /*
     * GetMemoryMap/ExitBootServices is retried with a fresh MapKey each time.
     * Firmware may partially shut down services after the first EBS call;
     * therefore, after that point this path uses only GetMemoryMap,
     * ExitBootServices, and the UEFI Memory Allocation Services used for
     * buffer growth. All protocol discovery and watchdog work is complete.
     *
     * Buffer growth and ExitBootServices attempts have separate limits:
     * growth must not consume the number of actual EBS attempts.
     */
    for (exit_attempt = 0;
         exit_attempt < EFI_EXIT_BOOT_SERVICES_MAX_ATTEMPTS;) {
        map_size = map_capacity;
        status = bs->GetMemoryMap(&map_size, map_buffer, &map_key,
                                  &descriptor_size, &descriptor_version);

        if (status == EFI_BUFFER_TOO_SMALL) {
            if (map_growth_attempt >= EFI_MEMORY_MAP_MAX_GROWTH_ATTEMPTS) {
                serial_puts("[QBOOT UEFI] memory-map growth limit reached\r\n");
                halt_forever();
            }
            ++map_growth_attempt;

            /* EFI returns the required byte count in map_size. */
            if (map_size == 0 ||
                map_size > UINT64_MAX - (EFI_PAGE_SIZE - 1ULL)) {
                serial_puts("[QBOOT UEFI] invalid required memory-map size\r\n");
                halt_forever();
            }

            required_map_pages =
                (map_size + EFI_PAGE_SIZE - 1ULL) / EFI_PAGE_SIZE;

            /* Reserve headroom: allocating the replacement can itself add
             * descriptors. Keep the full allocation below the 1-GiB map. */
            if (required_map_pages == 0 ||
                required_map_pages >
                    (0x40000000ULL / EFI_PAGE_SIZE) -
                    EFI_MEMORY_MAP_EXTRA_PAGES) {
                serial_puts("[QBOOT UEFI] required memory-map buffer too large\r\n");
                halt_forever();
            }

            new_map_pages =
                required_map_pages + EFI_MEMORY_MAP_EXTRA_PAGES;
            if (new_map_pages > UINT64_MAX / EFI_PAGE_SIZE) {
                serial_puts("[QBOOT UEFI] memory-map allocation overflow\r\n");
                halt_forever();
            }

            status = allocate_low_pages(
                bs, new_map_pages, &new_map_buffer_pa);
            if (EFI_ERROR(status)) {
                log_status("[QBOOT UEFI] memory-map growth allocation failed",
                           status);
                halt_forever();
            }

            /* Allocate replacement first; only then release the old block.
             * FreePages changes the map, so always fetch a new MapKey next. */
            map_free_status = bs->FreePages(map_buffer_pa, map_pages);
            if (EFI_ERROR(map_free_status)) {
                map_cleanup_status =
                    bs->FreePages(new_map_buffer_pa, new_map_pages);
                if (EFI_ERROR(map_cleanup_status)) {
                    log_status("[QBOOT UEFI] replacement buffer cleanup failed",
                               map_cleanup_status);
                }
                log_status("[QBOOT UEFI] old memory-map buffer release failed",
                           map_free_status);
                halt_forever();
            }

            map_buffer_pa = new_map_buffer_pa;
            map_buffer = (void *)(unsigned long long)map_buffer_pa;
            map_pages = new_map_pages;
            map_capacity = map_pages * EFI_PAGE_SIZE;

            serial_puts("[QBOOT UEFI] memory-map buffer expanded; retrying\r\n");
            continue;
        }

        if (EFI_ERROR(status)) {
            log_status("[QBOOT UEFI] GetMemoryMap failed", status);
            halt_forever();
        }

        /* DescriptorSize is a firmware-provided stride, not necessarily the
         * size of the prefix struct. Reject partial trailing descriptors. */
        if (descriptor_version != 1U ||
            descriptor_size < sizeof(EFI_MEMORY_DESCRIPTOR) ||
            map_size == 0 || map_size > map_capacity ||
            (map_size % descriptor_size) != 0) {
            serial_puts("[QBOOT UEFI] invalid firmware memory-map layout\r\n");
            halt_forever();
        }

        /* No allocations or unrelated Boot Services occur between this
         * successful GetMemoryMap and ExitBootServices. */
        ++exit_attempt;
        status = bs->ExitBootServices(image_handle, map_key);
        if (!EFI_ERROR(status)) {
            exited = 1;
            __asm__ volatile ("cli" ::: "memory");
            break;
        }

        if ((status & 0xFFFFULL) != EFI_INVALID_PARAMETER) {
            log_status("[QBOOT UEFI] ExitBootServices failed", status);
            halt_forever();
        }
        /* EFI_INVALID_PARAMETER: loop, refresh the map, and retry. */
    }
'''


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise PatchError(f"{label}: expected exactly one anchor, found {count}")
    return text.replace(old, new, 1)


def patch_efi_main(src: str) -> str:
    if "EFI_EXIT_BOOT_SERVICES_MAX_ATTEMPTS" in src and "exit_attempt" in src:
        raise PatchError("efi_main.c already appears to contain the integrated EBS repair")

    src = replace_once(
        src,
        "#define EFI_MEMORY_MAP_EXTRA_PAGES 16ULL\n",
        "#define EFI_MEMORY_MAP_EXTRA_PAGES 16ULL\n"
        "#define EFI_MEMORY_MAP_MAX_GROWTH_ATTEMPTS 8U\n"
        "#define EFI_EXIT_BOOT_SERVICES_MAX_ATTEMPTS 5U\n",
        "efi_main.c growth/EBS limits",
    )

    src = replace_once(
        src,
        "#define EFI_PAGE_TABLE_PAGES 68U\n",
        "#define EFI_PAGE_TABLE_PAGES 68U\n"
        "_Static_assert(EFI_PAGE_TABLE_PAGES == QM_BOOT_UEFI_PAGE_TABLE_PAGES,\n"
        "               \"UEFI bootstrap page-table allocation size\");\n",
        "efi_main.c shared page-table allocation assertion",
    )

    fn_start = src.find("EFI_STATUS efi_main(")
    if fn_start < 0:
        raise PatchError("efi_main.c: cannot locate efi_main")
    prefix, fn = src[:fn_start], src[fn_start:]
    fn = replace_once(
        fn,
        "    unsigned int attempt;\n",
        "    unsigned int exit_attempt;\n"
        "    unsigned int map_growth_attempt = 0;\n",
        "efi_main.c independent counters",
    )

    start_marker = "    /*\n     * Each ExitBootServices attempt uses a freshly retrieved MapKey."
    start = fn.find(start_marker)
    if start < 0:
        raise PatchError("efi_main.c: current ExitBootServices loop marker not found")
    end_marker = "    if (!exited) {\n"
    end = fn.find(end_marker, start)
    if end < 0:
        raise PatchError("efi_main.c: end of ExitBootServices loop not found")
    old_region = fn[start:end]
    if old_region.count("bs->ExitBootServices(image_handle, map_key)") != 1:
        raise PatchError("efi_main.c: expected one ExitBootServices call in the old loop")
    fn = fn[:start] + EXIT_BLOCK + fn[end:]

    fn = replace_once(
        fn,
        "    if (attempt != 0)\n",
        "    if (exit_attempt > 1U)\n",
        "efi_main.c MapKey retry success log",
    )

    if ("unsigned int attempt;" in fn or "for (attempt" in fn or
            "if (attempt" in fn):
        raise PatchError("efi_main.c: stale attempt counter reference remains in efi_main")
    if fn.count("bs->ExitBootServices(image_handle, map_key)") != 1:
        raise PatchError("efi_main.c: resulting EBS call count is not exactly one")
    if "EFI_MEMORY_MAP_MAX_GROWTH_ATTEMPTS" not in fn:
        raise PatchError("efi_main.c: growth bound not wired into the function")
    return prefix + fn


def patch_entry64(src: str) -> str:
    if "uefi_early_exception_halt" in src or "uefi_early_idt:" in src:
        raise PatchError("entry64.S already contains the UEFI emergency IDT repair")

    old = """    # Move onto the kernel's own mapped stack before replacing the firmware CR3.
    lea stack_top64(%rip), %rsp
    and $-16, %rsp
    xor %rbp, %rbp

    # QBoot Protocol v1.1 carries the root of the kernel's 1-GiB identity map.
    mov QM_BOOT_INFO_PAGE_TABLE_ROOT_OFFSET(%r12), %rax
    test %rax, %rax
    jz 2f
    mov %rax, %cr3

    lea __bss_start(%rip), %rdi
    lea __bss_end(%rip), %rcx
    sub %rdi, %rcx
    xor %eax, %eax
    rep stosb
"""
    new = """    # Clear BSS while firmware mappings and the firmware-provided stack are
    # still active. The emergency IDT storage lives in BSS and is populated
    # only after this clear.
    lea __bss_start(%rip), %rdi
    lea __bss_end(%rip), %rcx
    sub %rdi, %rcx
    xor %eax, %eax
    rep stosb

    # Move onto the kernel-owned stack before replacing the firmware CR3.
    lea stack_top64(%rip), %rsp
    and $-16, %rsp
    xor %rbp, %rbp

    # Install an emergency IDT before switching page tables. Every vector
    # enters a non-returning halt handler, so no firmware IDT memory is needed
    # after the new CR3 becomes active.
    leaq uefi_early_idt(%rip), %rdi
    leaq uefi_early_exception_halt(%rip), %r8
    movl $256, %ecx
.Luefi_early_idt_fill:
    movw %r8w, 0(%rdi)
    movw $0x08, 2(%rdi)
    movb $0, 4(%rdi)
    movb $0x8e, 5(%rdi)
    movq %r8, %rax
    shrq $16, %rax
    movw %ax, 6(%rdi)
    shrq $16, %rax
    movl %eax, 8(%rdi)
    movl $0, 12(%rdi)
    addq $16, %rdi
    loop .Luefi_early_idt_fill
    lidt uefi_early_idtr(%rip)

    # QBoot Protocol v1.1 carries the root of the kernel's 1-GiB identity map.
    mov QM_BOOT_INFO_PAGE_TABLE_ROOT_OFFSET(%r12), %rax
    test %rax, %rax
    jz 2f
    mov %rax, %cr3
"""
    src = replace_once(src, old, new,
                       "entry64.S BSS/stack/CR3/early-IDT transition")

    rodata_marker = ".section .rodata\n.align 8\nuefi_gdt:\n"
    rodata_add = (
        ".section .rodata\n"
        ".align 8\n"
        "uefi_early_idtr:\n"
        "    .word 4095\n"
        "    .quad uefi_early_idt\n"
        ".align 8\n"
        "uefi_gdt:\n"
    )
    src = replace_once(src, rodata_marker, rodata_add,
                       "entry64.S emergency IDTR descriptor")

    size_marker = ".size _start64_uefi, .-_start64_uefi\n"
    handler = (
        size_marker +
        "\n"
        ".type uefi_early_exception_halt, @function\n"
        "uefi_early_exception_halt:\n"
        "    cli\n"
        ".Luefi_early_exception_stop:\n"
        "    hlt\n"
        "    jmp .Luefi_early_exception_stop\n"
        ".size uefi_early_exception_halt, .-uefi_early_exception_halt\n"
    )
    src = replace_once(src, size_marker, handler,
                       "entry64.S emergency exception halt handler")

    src = src.rstrip() + (
        "\n\n.section .bss\n"
        ".align 16\n"
        "uefi_early_idt:\n"
        "    .skip 4096\n"
    )
    if src.count("uefi_early_idt:") != 1 or "lidt uefi_early_idtr(%rip)" not in src:
        raise PatchError("entry64.S resulting early-IDT/BSS structure is invalid")
    return src

def patch_boot_h(src: str) -> str:
    marker = "#define QM_BOOT_MAX_MEMORY_REGIONS 128U\n"
    addition = (
        "#define QM_BOOT_MAX_MEMORY_REGIONS 128U\n"
        "\n"
        "/* QBoot UEFI bootstrap allocates one contiguous 68-page paging block.\n"
        " * Keep this allocation reserved while it is the active CR3 hierarchy. */\n"
        "#define QM_BOOT_UEFI_PAGE_TABLE_PAGES 68U\n"
        "#define QM_BOOT_UEFI_PAGE_SIZE 4096ULL\n"
        "#define QM_BOOT_UEFI_PAGE_TABLE_SIZE \\\n"
        "    ((qm_u64_t)QM_BOOT_UEFI_PAGE_TABLE_PAGES * QM_BOOT_UEFI_PAGE_SIZE)\n"
    )
    if "QM_BOOT_UEFI_PAGE_TABLE_SIZE" in src:
        raise PatchError("boot.h already defines the UEFI page-table allocation size")
    return replace_once(src, marker, addition, "boot.h page-table allocation ABI constant")


def patch_boot_info_c(src: str) -> str:
    if "QM_BOOT_UEFI_PAGE_TABLE_SIZE" in src:
        raise PatchError("boot_info.c already validates the UEFI page-table allocation span")
    marker = "    if (info->memory_map_count == 0 || info->memory_map_physical == 0 ||\n"
    insert = (
        "    /* The active UEFI CR3 block is a single 68-page allocation.\n"
        "     * Validate the complete span, not only the root page address. */\n"
        "    if (info->protocol_minor >= 1U &&\n"
        "        info->firmware == QM_BOOT_FIRMWARE_UEFI &&\n"
        "        info->page_table_root_physical >\n"
        "            0x40000000ULL - QM_BOOT_UEFI_PAGE_TABLE_SIZE)\n"
        "        return QM_ERR_CORRUPT;\n"
        "\n"
        + marker
    )
    return replace_once(src, marker, insert, "boot_info.c active page-table span validation")


def patch_memory_c(src: str) -> str:
    if "MM_RESERVE_BOOT(info->page_table_root_physical" in src:
        raise PatchError("memory.c already reserves the active UEFI page-table block")
    marker = "    MM_RESERVE_BOOT(info->memory_map_physical, map_bytes);\n"
    addition = (
        marker +
        "\n"
        "    /* Do not let PMM reclaim the page tables currently loaded in CR3.\n"
        "     * The loader allocates the full contiguous 68-page block even when\n"
        "     * fewer split page tables are needed for the kernel image. */\n"
        "    if (info->firmware == QM_BOOT_FIRMWARE_UEFI &&\n"
        "        info->protocol_minor >= 1U &&\n"
        "        info->page_table_root_physical != 0) {\n"
        "        MM_RESERVE_BOOT(info->page_table_root_physical,\n"
        "                        QM_BOOT_UEFI_PAGE_TABLE_SIZE);\n"
        "    }\n"
    )
    return replace_once(src, marker, addition, "memory.c active page-table reservation")


def build_patches(root: Path) -> dict[Path, str]:
    targets = {
        "efi": root / "boot/uefi/efi_main.c",
        "boot": root / "include/qmjy/boot.h",
        "boot_info": root / "kernel/core/boot_info.c",
        "memory": root / "mm/physical/memory.c",
        "entry": root / "arch/x86_64/boot/entry64.S",
    }
    missing = [str(p) for p in targets.values() if not p.is_file()]
    if missing:
        raise PatchError("missing required source file(s): " + ", ".join(missing))

    # Prepare every transformation first. No target is written if any anchor fails.
    originals = {key: path.read_text(encoding="utf-8") for key, path in targets.items()}
    patched = {
        targets["boot"]: patch_boot_h(originals["boot"]),
        targets["boot_info"]: patch_boot_info_c(originals["boot_info"]),
        targets["memory"]: patch_memory_c(originals["memory"]),
        targets["entry"]: patch_entry64(originals["entry"]),
        targets["efi"]: patch_efi_main(originals["efi"]),
    }
    return patched


def write_transaction(root: Path, patched: dict[Path, str]) -> list[Path]:
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    backup_dir = root / "build/uefi-security-audit/backups"
    backup_dir.mkdir(parents=True, exist_ok=True)

    backups: list[Path] = []
    for path in patched:
        backup = backup_dir / f"{path.name}.{stamp}.bak"
        suffix = 1
        while backup.exists():
            backup = backup_dir / f"{path.name}.{stamp}.{suffix}.bak"
            suffix += 1
        shutil.copy2(path, backup)
        backups.append(backup)

    # Atomic replacement in each source directory; no /tmp use.
    staged: list[tuple[Path, Path]] = []
    try:
        for path, content in patched.items():
            tmp = path.with_name(path.name + ".qboot-repair.tmp")
            if tmp.exists():
                tmp.unlink()
            tmp.write_text(content, encoding="utf-8", newline="")
            staged.append((tmp, path))
        for tmp, target in staged:
            os.replace(tmp, target)
    except Exception:
        for tmp, _ in staged:
            try:
                if tmp.exists():
                    tmp.unlink()
            except OSError:
                pass
        # Restore all sources from the just-created backups if a replacement failed.
        for target, backup in zip(patched.keys(), backups):
            try:
                shutil.copy2(backup, target)
            except OSError:
                pass
        raise
    return backups


def self_test() -> None:
    """Run an isolated transform simulation against minimal source fixtures."""
    boot = "#define QM_BOOT_MAX_MEMORY_REGIONS 128U\n"
    boot_out = patch_boot_h(boot)
    assert "QM_BOOT_UEFI_PAGE_TABLE_SIZE" in boot_out

    bi = (
        "void f(void) {\n"
        "    if (info->memory_map_count == 0 || info->memory_map_physical == 0 ||\n"
        "        info->memory_map_entry_size < sizeof(qm_boot_memory_region_t))\n"
        "        return;\n"
        "}\n"
    )
    bi_out = patch_boot_info_c(bi)
    assert "0x40000000ULL - QM_BOOT_UEFI_PAGE_TABLE_SIZE" in bi_out

    mem = "void f(void) {\n    MM_RESERVE_BOOT(info->memory_map_physical, map_bytes);\n}\n"
    mem_out = patch_memory_c(mem)
    assert "MM_RESERVE_BOOT(info->page_table_root_physical" in mem_out

    efi_prefix = (
        "#define EFI_MEMORY_MAP_EXTRA_PAGES 16ULL\n"
        "#define EFI_PAGE_TABLE_PAGES 68U\n"
        "EFI_STATUS efi_main(void) {\n"
        "    unsigned int attempt;\n"
        "    int exited = 0;\n"
        "    /*\n"
        "     * Each ExitBootServices attempt uses a freshly retrieved MapKey.\n"
        "     */\n"
        "    for (attempt = 0; attempt < 5U; ++attempt) {\n"
        "        status = bs->GetMemoryMap(&map_size, map_buffer, &map_key, &descriptor_size, &descriptor_version);\n"
        "        status = bs->ExitBootServices(image_handle, map_key);\n"
        "    }\n"
        "    if (!exited) {\n"
        "        halt_forever();\n"
        "    }\n"
        "    if (attempt != 0)\n"
        "        serial_puts(\"retry\");\n"
        "}\n"
    )
    # The test fixture only verifies the lexical transformations; the production
    # source has the full body and is protected by exact marker/count checks.
    efi_out = patch_efi_main(efi_prefix)
    assert "for (exit_attempt = 0;" in efi_out
    assert "map_growth_attempt = 0" in efi_out
    assert "(map_size % descriptor_size) != 0" in efi_out
    assert "if (exit_attempt > 1U)" in efi_out
    assert re.search(r"\battempt\b", efi_out) is None

    entry = """    # Move onto the kernel's own mapped stack before replacing the firmware CR3.
    lea stack_top64(%rip), %rsp
    and $-16, %rsp
    xor %rbp, %rbp

    # QBoot Protocol v1.1 carries the root of the kernel's 1-GiB identity map.
    mov QM_BOOT_INFO_PAGE_TABLE_ROOT_OFFSET(%r12), %rax
    test %rax, %rax
    jz 2f
    mov %rax, %cr3

    lea __bss_start(%rip), %rdi
    lea __bss_end(%rip), %rcx
    sub %rdi, %rcx
    xor %eax, %eax
    rep stosb
.size _start64_uefi, .-_start64_uefi
.section .rodata
.align 8
uefi_gdt:
"""
    entry_out = patch_entry64(entry)
    assert "lidt uefi_early_idtr(%rip)" in entry_out
    assert "uefi_early_idt:\n    .skip 4096" in entry_out
    print("INTEGRATED PATCH SIMULATION: PASS (isolated source fixtures)")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path.home() / "qmjyOS",
                        help="qmjyOS repository root (default: ~/qmjyOS)")
    parser.add_argument("--dry-run", action="store_true",
                        help="show proposed diffs without writing source files")
    parser.add_argument("--self-test", action="store_true",
                        help="run isolated patch-transform simulation only")
    args = parser.parse_args()

    if args.self_test:
        self_test()
        return 0

    root = args.root.expanduser().resolve()
    if not root.is_dir():
        print(f"[ABORT] repository directory not found: {root}", file=sys.stderr)
        return 2

    try:
        patched = build_patches(root)
        if args.dry_run:
            for path, new in patched.items():
                old = path.read_text(encoding="utf-8")
                print(f"===== {path.relative_to(root)} =====")
                sys.stdout.writelines(difflib.unified_diff(
                    old.splitlines(True), new.splitlines(True),
                    fromfile="before", tofile="after"))
            print("[DRY RUN] all anchors matched; no files changed")
            return 0
        backups = write_transaction(root, patched)
    except (PatchError, OSError, UnicodeError) as exc:
        print(f"[ABORT] {exc}\nNo source files were intentionally modified.", file=sys.stderr)
        return 3

    print("[PATCH OK] UEFI integrated repair applied")
    for path in patched:
        print(f"  modified: {path.relative_to(root)}")
    print("[BACKUPS]")
    for backup in backups:
        print(f"  {backup.relative_to(root)}")
    print("[NOTE] No build, test, git, or QEMU command was executed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
