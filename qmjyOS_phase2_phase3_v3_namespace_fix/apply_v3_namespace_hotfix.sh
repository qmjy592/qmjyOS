#!/usr/bin/env bash
set -Eeuo pipefail
SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
PROJECT_ROOT="$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd -P)"
V3_DIR="$PROJECT_ROOT/qmjyOS_phase2_phase3_progress_v3"
MODE="${1:-apply}"

fail() { printf '[FAIL] %s\n' "$*" >&2; exit 1; }

[[ -d "$V3_DIR/phase2_foundation" ]] || fail "找不到旧 v3 补丁目录：$V3_DIR/phase2_foundation。请先将原 v3 ZIP 解压到 qmjyOS 项目根目录。"
[[ -f "$V3_DIR/apply_phase2_all_v3.sh" ]] || fail "找不到原 v3 整合入口：$V3_DIR/apply_phase2_all_v3.sh"

printf '[V3 API NAMESPACE HOTFIX] Project root: %s\n' "$PROJECT_ROOT"
printf '[V3 API NAMESPACE HOTFIX] 修复内核初始化 API 与现有 qm_init 服务 API 的命名冲突。\n'

python3 - "$PROJECT_ROOT" "$V3_DIR" "$MODE" <<'PY'
from pathlib import Path
from datetime import datetime
import os, re, shutil, sys, tempfile

root = Path(sys.argv[1]).resolve()
v3 = Path(sys.argv[2]).resolve()
mode = sys.argv[3]

required = [
    v3 / "phase2_foundation/include/qmjy/kernel.h",
    v3 / "phase2_foundation/kernel/core/init_registry.c",
    v3 / "phase2_foundation/apply_phase2_foundation.sh",
]
missing = [str(p) for p in required if not p.is_file()]
if missing:
    raise SystemExit("[FAIL] v3 补丁文件结构不完整：\n" + "\n".join(missing))

# Namespace the new kernel-core init registry. The pre-existing init/startup
# service registry keeps its public qm_init_* API unchanged.
replacements = [
    ("qm_init_level_t", "qm_kernel_init_level_t"),
    ("qm_init_fn_t", "qm_kernel_init_fn_t"),
    ("qm_init_entry_t", "qm_kernel_init_entry_t"),
    ("qm_init_run_level", "qm_kernel_init_run_level"),
    ("qm_init_register", "qm_kernel_init_register"),
    ("QM_INIT_EARLY", "QM_KERNEL_INIT_EARLY"),
    ("QM_INIT_ARCH", "QM_KERNEL_INIT_ARCH"),
    ("QM_INIT_MEMORY", "QM_KERNEL_INIT_MEMORY"),
    ("QM_INIT_CORE", "QM_KERNEL_INIT_CORE"),
    ("QM_INIT_DRIVER", "QM_KERNEL_INIT_DRIVER"),
    ("QM_INIT_FILESYSTEM", "QM_KERNEL_INIT_FILESYSTEM"),
    ("QM_INIT_LATE", "QM_KERNEL_INIT_LATE"),
]

def transform(data):
    for old, new in replacements:
        data = data.replace(old, new)
    return data

files = []
for p in v3.rglob("*"):
    if not p.is_file():
        continue
    try:
        data = p.read_text(encoding="utf-8")
    except (UnicodeDecodeError, OSError):
        continue
    changed = transform(data)
    if changed != data:
        files.append((p, data, changed))

# Check that the new API is present in the payload and old conflicting names
# are absent from its C/header/build/test sources.
header = transform((v3 / "phase2_foundation/include/qmjy/kernel.h").read_text(encoding="utf-8"))
impl = transform((v3 / "phase2_foundation/kernel/core/init_registry.c").read_text(encoding="utf-8"))
for symbol in ("qm_kernel_init_register", "qm_kernel_init_run_level", "qm_kernel_init_entry_t", "qm_kernel_init_level_t"):
    if symbol not in header and symbol not in impl:
        raise SystemExit(f"[FAIL] 修复后缺少预期的新接口：{symbol}")

# Search the actual project for conflicting definitions/declarations of the
# new namespace before touching even the extracted patch payload.
new_function = re.compile(r"\bqm_kernel_init_register\s*\([^;{}]*\)\s*\{")
new_types = ("qm_kernel_init_level_t", "qm_kernel_init_fn_t", "qm_kernel_init_entry_t")
conflicts = []
for p in root.rglob("*"):
    if not p.is_file() or v3 == p or v3 in p.parents:
        continue
    if any(part in {".git", "build", "out", "dist"} for part in p.parts):
        continue
    if p.suffix not in {".c", ".h", ".cc", ".cpp"}:
        continue
    try:
        s = p.read_text(encoding="utf-8")
    except (UnicodeDecodeError, OSError):
        continue
    if new_function.search(s):
        conflicts.append(f"函数定义冲突：{p}")
    for t in new_types:
        if re.search(r"\btypedef\b[^;]*\b" + re.escape(t) + r"\s*;", s, re.S):
            conflicts.append(f"类型定义冲突 {t}：{p}")
if conflicts:
    raise SystemExit("[FAIL] 项目已存在新命名空间的定义，停止修复：\n" + "\n".join(conflicts))

# No write until every preflight above has succeeded.
if files:
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    backup_root = root / "build/repair-backups/phase2-v3-api-namespace" / stamp
    for p, old, new in files:
        rel = p.relative_to(v3)
        backup = backup_root / rel
        backup.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(p, backup)
        fd, tmp_name = tempfile.mkstemp(prefix=p.name + ".", suffix=".new", dir=p.parent)
        try:
            with os.fdopen(fd, "w", encoding="utf-8", newline="") as f:
                f.write(new)
                f.flush()
                os.fsync(f.fileno())
            os.chmod(tmp_name, p.stat().st_mode)
            os.replace(tmp_name, p)
        finally:
            if os.path.exists(tmp_name):
                os.unlink(tmp_name)
    print(f"[PASS] 已修正补丁包内 {len(files)} 个文件。")
    print(f"[BACKUP] {backup_root}")
else:
    print("[ALREADY] 补丁包命名空间已修正，无需重复改写。")

# Verify the resulting payload, including shell scripts and C tests.
for p in v3.rglob("*"):
    if not p.is_file() or p.suffix not in {".c", ".h", ".py", ".sh", ".md", ".txt"}:
        continue
    try:
        s = p.read_text(encoding="utf-8")
    except (UnicodeDecodeError, OSError):
        continue
    if "qm_init_register" in s or "qm_init_entry_t" in s or "qm_init_level_t" in s:
        raise SystemExit(f"[FAIL] 补丁包仍残留旧的冲突标识符：{p}")

if mode == "--patch-only":
    print("[PASS] 仅修正补丁包完成；未应用项目源码补丁。")
PY

if [[ "$MODE" == "--patch-only" ]]; then
    exit 0
fi
if [[ "$MODE" != "apply" ]]; then
    fail "未知参数：$MODE（可选 --patch-only）"
fi

printf '[V3 API NAMESPACE HOTFIX] 开始执行已修正的 v3 整合安装器。\n'
bash "$V3_DIR/apply_phase2_all_v3.sh"
