#!/usr/bin/env python3
from pathlib import Path
from datetime import datetime
import sys

ROOT = Path(__file__).resolve().parents[2]
BACKUP_ROOT = ROOT / "build" / "overall-repair-work"

def replace_once(source: str, old: str, new: str, label: str) -> str:
    count = source.count(old)
    if count == 1:
        return source.replace(old, new, 1)
    if count == 0 and new in source:
        return source
    raise RuntimeError(
        f"{label}: expected exactly one old anchor, found {count}"
    )

def read_file(relative: str) -> str:
    path = ROOT / relative
    if not path.is_file():
        raise RuntimeError(f"Source file missing: {relative}")
    return path.read_text(encoding="utf-8")

def main() -> int:
    # ---------------------------------------------------------
    # 1. MBR scanner: stage results before copying to caller.
    #    This prevents NULL dereference with capacity == 0 and
    #    prevents partial output on a failed scan.
    # ---------------------------------------------------------
    mbr_path = "fs/partition/mbr.c"
    mbr = read_file(mbr_path)

    mbr = replace_once(
        mbr,
        "    qm_block_device_info_t device_info;\n",
        "    qm_block_device_info_t device_info;\n"
        "    qm_partition_t parsed[QM_MBR_PARTITION_COUNT];\n",
        "MBR local staging array"
    )

    mbr = replace_once(
        mbr,
        "out_partitions[j].start_lba",
        "parsed[j].start_lba",
        "MBR overlap start"
    )
    mbr = replace_once(
        mbr,
        "out_partitions[j].sector_count",
        "parsed[j].sector_count",
        "MBR overlap end"
    )

    mbr = replace_once(
        mbr,
        "        if (found >= capacity)\n"
        "            return -3;\n\n",
        "",
        "MBR early capacity check"
    )

    for field in (
        "device_id", "index", "type",
        "bootable", "start_lba", "sector_count"
    ):
        mbr = replace_once(
            mbr,
            f"out_partitions[found].{field}",
            f"parsed[found].{field}",
            f"MBR staged field {field}"
        )

    mbr = replace_once(
        mbr,
        "    *out_count = found;\n"
        "    return 0;\n",
        "    /* Publish results only after the entire table is valid. */\n"
        "    if (found > capacity)\n"
        "        return -3;\n"
        "\n"
        "    for (i = 0; i < found; ++i)\n"
        "        out_partitions[i] = parsed[i];\n"
        "\n"
        "    *out_count = found;\n"
        "    return 0;\n",
        "MBR atomic result publication"
    )

    # ---------------------------------------------------------
    # 2. QFS: require exact agreement between the data bitmap
    #    and the set of blocks owned by valid inodes.
    # ---------------------------------------------------------
    qfs_path = "fs/qfs/qfs.c"
    qfs = read_file(qfs_path)

    old_qfs_anchor = (
        "                } else if (block != 0) {\n"
        "                    qfs_is_mounted = 0;\n"
        "                    return VFS_ERR_INVALID;\n"
        "                }\n"
        "            }\n"
        "        }\n"
        "    }\n\n"
        "    /*\n"
        "     * Inode zero is reserved and must never represent a file.\n"
    )

    new_qfs_anchor = (
        "                } else if (block != 0) {\n"
        "                    qfs_is_mounted = 0;\n"
        "                    return VFS_ERR_INVALID;\n"
        "                }\n"
        "            }\n"
        "        }\n"
        "\n"
        "        /* Every allocated data bit must belong to exactly one inode. */\n"
        "        for (i = QFS_DATA_START; i < qfs_total_sectors; ++i) {\n"
        "            if (qfs_bitmap_test(i) != (block_seen[i] != 0)) {\n"
        "                qfs_is_mounted = 0;\n"
        "                return VFS_ERR_INVALID;\n"
        "            }\n"
        "        }\n"
        "\n"
        "        /* Bits beyond the partition boundary must never be allocated. */\n"
        "        for (i = qfs_total_sectors;\n"
        "             i < QFS_BITMAP_SECTORS * QFS_SECTOR_SIZE * 8U;\n"
        "             ++i) {\n"
        "            if (qfs_bitmap_test(i)) {\n"
        "                qfs_is_mounted = 0;\n"
        "                return VFS_ERR_INVALID;\n"
        "            }\n"
        "        }\n"
        "    }\n\n"
        "    /*\n"
        "     * Inode zero is reserved and must never represent a file.\n"
    )

    qfs = replace_once(
        qfs, old_qfs_anchor, new_qfs_anchor,
        "QFS reverse bitmap validation"
    )

    # ---------------------------------------------------------
    # 3. QFS host test: deliberately inject two corrupt bitmap
    #    states, require mount rejection, restore and remount.
    # ---------------------------------------------------------
    host_test_path = "tests/filesystem/qfs_host_test.c"
    host_test = read_file(host_test_path)

    old_host_anchor = (
        "    CHECK(\n"
        "        qfs_unmount() == VFS_OK,\n"
        "        \"final QFS unmount\");\n\n"
        "    CHECK(\n"
        "        qm_block_device_unregister(TEST_DEVICE_ID) == QM_OK,\n"
    )

    new_host_anchor = (
        "    CHECK(\n"
        "        qfs_unmount() == VFS_OK,\n"
        "        \"final QFS unmount\");\n\n"
        "    {\n"
        "        const size_t bitmap_base =\n"
        "            ((size_t)TEST_PARTITION_START + 1U) * TEST_SECTOR_SIZE;\n"
        "        const size_t orphan_byte = bitmap_base + 30U / 8U;\n"
        "        const size_t outside_byte =\n"
        "            bitmap_base + TEST_PARTITION_SECTORS / 8U;\n"
        "\n"
        "        /* Block 30 is inside the partition but has no inode owner. */\n"
        "        test_disk[orphan_byte] |= (unsigned char)(1U << (30U % 8U));\n"
        "        CHECK(\n"
        "            qfs_mount(&partition) == VFS_ERR_INVALID,\n"
        "            \"reject allocated data block without inode owner\");\n"
        "        test_disk[orphan_byte] &=\n"
        "            (unsigned char)~(1U << (30U % 8U));\n"
        "\n"
        "        /* Sector 2048 is exactly outside this partition's extent. */\n"
        "        test_disk[outside_byte] |= 1U;\n"
        "        CHECK(\n"
        "            qfs_mount(&partition) == VFS_ERR_INVALID,\n"
        "            \"reject bitmap allocation beyond partition boundary\");\n"
        "        test_disk[outside_byte] &= (unsigned char)~1U;\n"
        "\n"
        "        CHECK(\n"
        "            qfs_mount(&partition) == VFS_OK,\n"
        "            \"mount succeeds after restoring valid bitmap\");\n"
        "        CHECK(\n"
        "            qfs_unmount() == VFS_OK,\n"
        "            \"unmount after bitmap corruption tests\");\n"
        "    }\n\n"
        "    CHECK(\n"
        "        qm_block_device_unregister(TEST_DEVICE_ID) == QM_OK,\n"
    )

    host_test = replace_once(
        host_test, old_host_anchor, new_host_anchor,
        "QFS bitmap corruption host tests"
    )

    # ---------------------------------------------------------
    # 4. QFS image builder: emit linker map and symbol table.
    #    The independent test kernel must contain both _start64
    #    and its own test main() symbol.
    # ---------------------------------------------------------
    builder_path = "tools/qboot/build_qfs_ide_images.sh"
    builder = read_file(builder_path)

    builder = replace_once(
        builder,
        "for tool in clang ld.lld llvm-objcopy python3 make; do\n",
        "for tool in clang ld.lld llvm-objcopy llvm-nm python3 make; do\n",
        "QFS builder llvm-nm dependency"
    )

    builder = replace_once(
        builder,
        '    local test_object="$BUILD_ROOT/$name/qfs_ide_test_main.o"\n',
        '    local test_object="$BUILD_ROOT/$name/qfs_ide_test_main.o"\n'
        '    local kernel_map="$BUILD_ROOT/$name/kernel64.map"\n'
        '    local kernel_symbols="$BUILD_ROOT/$name/kernel64.symbols"\n',
        "QFS builder map paths"
    )

    builder = replace_once(
        builder,
        '        -T linker64.ld \\\n'
        '        -nostdlib -static \\\n'
        '        -o "$kernel_elf" \\\n',
        '        -T linker64.ld \\\n'
        '        -Map="$kernel_map" \\\n'
        '        -nostdlib -static \\\n'
        '        -o "$kernel_elf" \\\n',
        "QFS linker map generation"
    )

    builder = replace_once(
        builder,
        '    llvm-objcopy -O binary "$kernel_elf" "$kernel_bin"\n\n'
        '    [ -s "$kernel_bin" ] ||\n',
        '    llvm-nm -n --defined-only "$kernel_elf" > "$kernel_symbols"\n'
        '\n'
        '    grep -Eq "[[:space:]][Tt][[:space:]]+_start64$" "$kernel_symbols" ||\n'
        '        fail "64-bit entry symbol _start64 missing in $kernel_elf"\n'
        '\n'
        '    grep -Eq "[[:space:]][Tt][[:space:]]+main$" "$kernel_symbols" ||\n'
        '        fail "QFS test main() symbol missing in $kernel_elf"\n'
        '\n'
        '    [ -s "$kernel_map" ] ||\n'
        '        fail "Linker map was not generated: $kernel_map"\n'
        '\n'
        '    llvm-objcopy -O binary "$kernel_elf" "$kernel_bin"\n\n'
        '    [ -s "$kernel_bin" ] ||\n',
        "QFS entry/main symbol verification"
    )

    # ---------------------------------------------------------
    # 5. QFS test runner: preserve logs and explicitly require
    #    the first C marker after the assembly entry markers.
    # ---------------------------------------------------------
    runner_path = "tools/qboot/test_qfs_ide.sh"
    runner = read_file(runner_path)

    runner = replace_once(
        runner,
        "for tool in qemu-system-x86_64 timeout mktemp python3 dd sha256sum cut; do\n",
        "for tool in qemu-system-x86_64 timeout python3 dd sha256sum cut; do\n",
        "Remove unused mktemp dependency"
    )

    runner = replace_once(
        runner,
        'cleanup() {\n'
        '    rm -rf "$TEST_DIR"\n'
        '}\n'
        'trap cleanup EXIT\n',
        'cleanup() {\n'
        '    echo "[QFS IDE TEST] Runtime artifacts retained at: $TEST_DIR"\n'
        '}\n'
        'trap cleanup EXIT\n',
        "Preserve QFS runtime logs and test disk"
    )

    runner = replace_once(
        runner,
        '    require_marker "$debug_log" "QBOOT_LONG_MODE_HANDOFF"\n',
        '    require_marker "$debug_log" "QBOOT_LONG_MODE_HANDOFF"\n'
        '    require_marker "$debug_log" "[QFS] MAIN_ENTER"\n',
        "QFS C entry marker check"
    )

    # ---------------------------------------------------------
    # 6. Ring 3 cleanup: never release a physical page if its
    #    virtual mapping could not be removed successfully.
    # ---------------------------------------------------------
    main_path = "kernel/core/main.c"
    kernel_main = read_file(main_path)

    kernel_main = replace_once(
        kernel_main,
        "    int activated = 0;\n"
        "    int result = -1;\n",
        "    int activated = 0;\n"
        "    int stack_page_releasable = 1;\n"
        "    int code_page_releasable = 1;\n"
        "    int result = -1;\n",
        "Ring 3 safe release state"
    )

    slash = chr(92)
    lines = kernel_main.splitlines(keepends=True)

    def replace_warning(marker, flag, label):
        matches = [
            i for i, line in enumerate(lines)
            if marker in line
        ]

        if len(matches) != 1:
            raise RuntimeError(
                f"{label}: expected one source line, found {len(matches)}"
            )

        index = matches[0]
        if "page retained" in lines[index]:
            if index + 1 >= len(lines) or flag not in lines[index + 1]:
                raise RuntimeError(f"{label}: partially patched source")
            return

        original = lines[index]
        indent = original[:len(original) - len(original.lstrip())]
        newline = "\r\n" if original.endswith("\r\n") else "\n"

        lines[index] = (
            indent
            + 'serial_puts("[Ring 3] WARNING: '
            + marker
            + '; page retained.'
            + slash + 'n");'
            + newline
        )
        lines.insert(index + 1, indent + flag + " = 0;" + newline)

    replace_warning(
        "stack unmap failed.",
        "stack_page_releasable",
        "Ring 3 stack unmap failure handling"
    )

    replace_warning(
        "code unmap failed.",
        "code_page_releasable",
        "Ring 3 code unmap failure handling"
    )

    kernel_main = "".join(lines)

    kernel_main = replace_once(
        kernel_main,
        "    if (stack_pa && mm_free_page(stack_pa) != MM_OK) {\n",
        "    if (stack_pa && stack_page_releasable &&\n"
        "        mm_free_page(stack_pa) != MM_OK) {\n",
        "Ring 3 stack release guard"
    )

    kernel_main = replace_once(
        kernel_main,
        "    if (code_pa && mm_free_page(code_pa) != MM_OK) {\n",
        "    if (code_pa && code_page_releasable &&\n"
        "        mm_free_page(code_pa) != MM_OK) {\n",
        "Ring 3 code release guard"
    )

    # All source anchors are verified before the first write.
    patched = {
        mbr_path: mbr,
        qfs_path: qfs,
        host_test_path: host_test,
        builder_path: builder,
        runner_path: runner,
        main_path: kernel_main,
    }

    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    backup_dir = BACKUP_ROOT / f"phase13-storage-integrity-{stamp}"
    backup_dir.mkdir(parents=True, exist_ok=False)

    for relative in patched:
        source_path = ROOT / relative
        backup_path = backup_dir / relative
        backup_path.parent.mkdir(parents=True, exist_ok=True)
        backup_path.write_bytes(source_path.read_bytes())

    for relative, content in patched.items():
        target = ROOT / relative
        target.write_text(content, encoding="utf-8")

    print("[PHASE 13] Storage integrity and boot diagnostics patched.")
    print(f"[PHASE 13] Backup: {backup_dir.relative_to(ROOT)}")
    print("[PHASE 13] Updated files:")
    for relative in patched:
        print(f"  - {relative}")
    print("[PHASE 13] No build directory or existing backup was deleted.")
    print("[PHASE 13] Next: run the validation commands shown below.")
    return 0

if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:
        print(f"[PHASE 13 FAIL] {exc}", file=sys.stderr)
        sys.exit(1)
