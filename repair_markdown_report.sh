#!/data/data/com.termux/files/usr/bin/bash
set -euo pipefail

REPORT="RING3_VALIDATION_2026-11-10.md"

if [ ! -f "$REPORT" ]; then
    echo "[ERROR] 验收报告不存在"
    exit 1
fi

BACKUP="markdown-backup/report-final-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$BACKUP"
cp -p "$REPORT" "$BACKUP/"

TMP="$(mktemp)"

cat > "$TMP" <<'REPORT_CONTENT'
## QEMU 实际运行验收补充（2026-11-10）

### 实际运行结果

本次在 QEMU 中完成了 Ring 0 → Ring 3 → Ring 0 的基础特权级往返测试。

关键运行输出：

```text
[Ring 3] Preparing isolated user address space.
[Ring 3] User code page: RX, U/S=1.
[Ring 3] User stack page: RW/NX, U/S=1.
[Ring 3] Kernel identity mapping: supervisor-only.
[Ring 3] CR3 activated.
[Ring 3] Entering CPL3.
[Ring 3] System call received successfully.
[Ring 3] Returning to Ring 0.
[Ring 3] Returned to Ring 0 successfully.
[Ring 3] Basic privilege transition: PASS.
Cooperative scheduler initialized (2 tasks).
Starting cooperative Ring 0 scheduler.
[task A] running
[task B] running
抢占式调度。
注意：make test-paging 是主机端页表逻辑测试，不能单独作为真实 CPU 权限隔离的证明。
REPORT_CONTENT
if grep -q "QEMU 实际运行验收补充（2026-11-10）" "$REPORT"; then
awk '
BEGIN
/^## QEMU 实际运行验收补充（2026-11-10）$/
skip && /^## /
!skip
' "$REPORT" > "$REPORT.tmp"
mv "$REPORT.tmp" "$REPORT"
fi
cat "$TMP" >> "$REPORT"
rm -f "$TMP"
echo "[OK] 验收报告更新完成"
echo "[INFO] 备份位置：$BACKUP"
echo
echo "===== 检查文档 ====="
grep -n "QEMU 实际运行验收补充" "$REPORT"
echo
echo "===== Git 差异 ====="
git --no-pager diff --stat -- README.md "$REPORT"
echo
echo "[完成] 未修改内核源码，未执行 commit 或 push。"
