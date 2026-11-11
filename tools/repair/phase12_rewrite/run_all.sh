#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
cd "$ROOT"

STAMP="$(date +%Y%m%d-%H%M%S)"
WORK="build/phase12-rewrite"
BACKUP="$WORK/backups/$STAMP"
REPORT="$WORK/reports/$STAMP"

mkdir -p "$BACKUP" "$REPORT"

log() {
    echo "[$(date '+%H:%M:%S')] $*"
}

fail() {
    log "[FAIL] $*"
    exit 1
}

log "Phase 1 + Phase 2 integrated repair"
log "工作目录：$WORK"

# 1. 保存当前源码快照，不覆盖旧备份。
log "保存当前源码快照"

tar -czf "$BACKUP/source-before.tar.gz" \
    --exclude='./.git' \
    --exclude='./build' \
    --exclude='*.o' \
    --exclude='*.d' \
    Makefile arch drivers fs kernel include init services tests \
    2>"$REPORT/backup.log" || {
        cat "$REPORT/backup.log"
        fail "源码快照失败"
    }

# 2. 检查现有修复脚本语法。
log "检查已有修复脚本"

find tools/repair -type f -name '*.py' -print0 |
while IFS= read -r -d '' file; do
    python3 -m py_compile "$file" || exit 1
done || fail "Python 修复脚本语法检查失败"

find tools/repair -type f -name '*.sh' -print0 |
while IFS= read -r -d '' file; do
    bash -n "$file" || exit 1
done || fail "Shell 修复脚本语法检查失败"

# 3. 执行已有阶段一只读审查。
if [ -f tools/repair/phase1_kernel.py ]; then
    log "运行阶段一内核检查"
    python3 tools/repair/phase1_kernel.py \
        >"$REPORT/phase1-audit.log" 2>&1 ||
        fail "阶段一检查失败"
fi

# 4. 检查模块配置。
if [ -f tools/qbuild/module_plan.py ]; then
    log "检查模块依赖"
    python3 tools/qbuild/module_plan.py --check \
        >"$REPORT/module-check.log" 2>&1 ||
        fail "模块配置或依赖检查失败"
fi

# 5. 检查补丁格式。
log "检查 Git 差异"
git diff --check >"$REPORT/git-diff-check.log" 2>&1 ||
    fail "git diff --check 失败"

# 6. 完整强制构建。
log "开始完整强制构建"
make -B -j2 >"$REPORT/full-build.log" 2>&1 || {
    tail -n 100 "$REPORT/full-build.log"
    fail "完整构建失败"
}

# 7. 验证关键产物。
for file in kernel.elf kernel64.elf kernel64.bin; do
    [ -s "$file" ] || fail "缺少构建产物：$file"
done

log "构建产物检查通过"

# 8. 运行现有测试，不覆盖既有测试目录。
if [ -f tools/qboot/test_qfs_ide.sh ]; then
    log "运行 QFS IDE 独立持久化测试"
    bash tools/qboot/test_qfs_ide.sh \
        >"$REPORT/qfs-ide-test.log" 2>&1 || {
            tail -n 120 "$REPORT/qfs-ide-test.log"
            log "QFS IDE 测试未通过，保留日志"
            exit 2
        }
fi

# 9. 最终状态。
git diff --check >"$REPORT/final-diff-check.log" 2>&1 ||
    fail "最终差异检查失败"

git status --short >"$REPORT/git-status.log"

log "执行结束"
log "备份：$BACKUP/source-before.tar.gz"
log "报告：$REPORT"
log "注意：构建成功不等于 QEMU 运行测试成功"
