#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
cd "$ROOT"

OUT="build/phase12-rewrite/reports/source_inventory.txt"

{
    echo "===== PROJECT ====="
    pwd

    echo
    echo "===== SOURCE FILES ====="
    find arch drivers fs kernel include init services tests \
        -type f \
        \( -name '*.c' -o -name '*.h' -o -name '*.S' -o -name '*.s' \) \
        -print 2>/dev/null | sort

    echo
    echo "===== BUILD CONFIGURATION ====="
    find . -maxdepth 3 -type f \
        \( -name 'Makefile' -o -name '*.mk' \
        -o -name 'modules.toml' -o -name 'qbuild.toml' \) \
        -not -path './.git/*' \
        -not -path './build/*' \
        -print 2>/dev/null | sort

    echo
    echo "===== IMPORTANT SYMBOLS ====="
    grep -R -n -E \
      'qm_ide_pio_init|qm_block_read|qm_block_write|qm_block_flush|qfs_format|qfs_mount|vm_map_page|vm_unmap_page|task_init|task_yield|idt_init|init_gdt_and_tss|main\\(' \
      arch drivers fs kernel include init services tests \
      --include='*.c' --include='*.h' --include='*.S' \
      2>/dev/null || true

    echo
    echo "===== GIT STATUS ====="
    git status --short 2>/dev/null || true
} > "$OUT"

echo "[PASS] 本地源码清单已生成：$OUT"
echo "[INFO] 文件数量：$(find arch drivers fs kernel include init services tests \
    -type f 2>/dev/null | wc -l)"
