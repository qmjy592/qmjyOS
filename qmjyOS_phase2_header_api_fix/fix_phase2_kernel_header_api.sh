#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(pwd)"
if [[ ! -f "$ROOT/include/qmjy/kernel.h" || ! -f "$ROOT/Makefile" ]]; then
    echo "[FAIL] 请在 ~/qmjyOS 项目根目录运行本脚本。"
    exit 1
fi

echo "[PHASE 2 HEADER API FIX] Project root: $ROOT"

python3 - <<'PY'
from pathlib import Path
from datetime import datetime
import os
import re
import shutil
import sys

root = Path.cwd()
header = root / "include/qmjy/kernel.h"
text = header.read_text(encoding="utf-8")

old_types = '''typedef enum qm_init_level {
    QM_INIT_EARLY = 0,
    QM_INIT_ARCH = 1,
    QM_INIT_MEMORY = 2,
    QM_INIT_CORE = 3,
    QM_INIT_DRIVER = 4,
    QM_INIT_FILESYSTEM = 5,
    QM_INIT_LATE = 6
} qm_init_level_t;

typedef qm_status_t (*qm_init_fn_t)(void);

typedef struct qm_init_entry {
    const char *name;
    qm_init_level_t level;
    qm_init_fn_t initialize;
} qm_init_entry_t;'''

new_types = '''typedef enum qm_kernel_init_level {
    QM_KERNEL_INIT_EARLY = 0,
    QM_KERNEL_INIT_ARCH = 1,
    QM_KERNEL_INIT_MEMORY = 2,
    QM_KERNEL_INIT_CORE = 3,
    QM_KERNEL_INIT_DRIVER = 4,
    QM_KERNEL_INIT_FILESYSTEM = 5,
    QM_KERNEL_INIT_LATE = 6
} qm_kernel_init_level_t;

typedef qm_status_t (*qm_kernel_init_fn_t)(void);

typedef struct qm_kernel_init_entry {
    const char *name;
    qm_kernel_init_level_t level;
    qm_kernel_init_fn_t initialize;
} qm_kernel_init_entry_t;'''

old_prototypes = '''qm_status_t qm_init_register(const qm_init_entry_t *entry);
qm_status_t qm_init_run_level(qm_init_level_t level);'''
new_prototypes = '''qm_status_t qm_kernel_init_register(const qm_kernel_init_entry_t *entry);
qm_status_t qm_kernel_init_run_level(qm_kernel_init_level_t level);'''

if new_types in text and new_prototypes in text:
    print("[ALREADY] include/qmjy/kernel.h 已包含完整的 qm_kernel_init_* API。")
else:
    if text.count(old_types) != 1:
        print("[FAIL] kernel.h 中旧初始化类型定义块不是唯一的；拒绝猜测性修改。", file=sys.stderr)
        sys.exit(2)
    if text.count(old_prototypes) != 1:
        print("[FAIL] kernel.h 中旧初始化函数声明块不是唯一的；拒绝猜测性修改。", file=sys.stderr)
        sys.exit(2)
    if "qm_kernel_init_register" in text or "qm_kernel_init_entry_t" in text:
        print("[FAIL] 检测到部分新 API，文件状态不符合完整旧版锚点；拒绝自动拼接。", file=sys.stderr)
        sys.exit(2)

    updated = text.replace(old_types, new_types, 1).replace(old_prototypes, new_prototypes, 1)

    required = [
        "qm_kernel_init_level_t", "qm_kernel_init_fn_t",
        "qm_kernel_init_entry_t", "QM_KERNEL_INIT_EARLY",
        "QM_KERNEL_INIT_LATE", "qm_kernel_init_register",
        "qm_kernel_init_run_level",
    ]
    missing = [item for item in required if item not in updated]
    if missing:
        print("[FAIL] 修改后验证缺少符号：" + ", ".join(missing), file=sys.stderr)
        sys.exit(3)

    if re.search(r"\bqm_init_(?:level_t|fn_t|entry_t|register|run_level)\b", updated):
        print("[FAIL] 修改后仍残留旧内核初始化 API 标识；拒绝写入。", file=sys.stderr)
        sys.exit(3)

    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    backup_dir = root / "build/repair-backups/phase2-header-api-fix" / stamp
    backup_dir.mkdir(parents=True, exist_ok=False)
    backup = backup_dir / "kernel.h.before-header-api-fix"
    shutil.copy2(header, backup)

    temp = header.with_name(header.name + ".phase2-api-fix.tmp")
    temp.write_text(updated, encoding="utf-8", newline="")
    os.replace(temp, header)
    print(f"[BACKUP] {backup}")
    print("[UPDATED] include/qmjy/kernel.h")

print("[PASS] Header API declarations verified.")
PY

echo "[PHASE 2 HEADER API FIX] Rebuilding foundation host tests."
mkdir -p build/tests/kernel-foundation

clang -std=c11 -Wall -Wextra -Werror -Iinclude \
    -o build/tests/kernel-foundation/kernel-foundation \
    tests/kernel/kernel_foundation_test.c \
    kernel/core/boot_info.c kernel/core/init_registry.c

clang -std=c11 -Wall -Wextra -Werror -Iinclude \
    -o build/tests/kernel-foundation/init-failure \
    tests/kernel/init_failure_test.c \
    kernel/core/boot_info.c kernel/core/init_registry.c

echo "[RUN] kernel-foundation"
build/tests/kernel-foundation/kernel-foundation

echo "[RUN] init-failure"
build/tests/kernel-foundation/init-failure

echo "[PHASE 2 HEADER API FIX] Running complete project build."
make -j2 all

echo "[PHASE 2 HEADER API FIX] Checking patch whitespace."
git diff --check

echo "[PHASE 2 HEADER API FIX PASS]"
echo "说明：本脚本只修复 foundation 的公共头文件 API 声明，并重跑相关测试/完整构建。"
echo "MM_HEAP 与 BIOS NXE 清理是否已应用，需以后续阶段日志确认；本脚本不擅自执行它们。"
