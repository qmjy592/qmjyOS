#!/data/data/com.termux/files/usr/bin/bash
set -euo pipefail

echo "===== qmjyOS Markdown 文档更新 ====="

ROOT="$(git rev-parse --show-toplevel 2>/dev/null)" || {
    echo "[ERROR] 当前目录不是 Git 仓库"
    exit 1
}
cd "$ROOT"

README="README.md"
REPORT="RING3_VALIDATION_2026-11-10.md"

for f in "$README" "$REPORT"; do
    if [ ! -f "$f" ]; then
        echo "[ERROR] 找不到文件：$f"
        exit 1
    fi
done

# 1. 备份
STAMP="$(date +%Y%m%d-%H%M%S)"
BACKUP="markdown-backup/$STAMP"
mkdir -p "$BACKUP"

cp -p README.md "$BACKUP/"
cp -p "$REPORT" "$BACKUP/"

echo "[OK] 原文件备份完成：$BACKUP"

# 2. 更新 README
python3 - <<'PY'
from pathlib import Path
import re

p = Path("README.md")
s = p.read_text(encoding="utf-8")

start = "<!-- QMjyOS-RING3-STATUS-START -->"
end = "<!-- QMjyOS-RING3-STATUS-END -->"

section = r"""
## 最新开发进度（2026-11-10）

### Ring 3 基础特权级切换：已通过 QEMU 验收

qmjyOS 已经在 QEMU 中完成真实的 Ring 0 → Ring 3 → Ring 0 特权级往返测试，并在返回内核态后继续运行原有 Ring 0 协作式双任务调度器。

目前已经实现：

- GDT 与 TSS 初始化。
- 配置 TSS.RSP0 内核栈。
- 使用 `iretq` 从 Ring 0 进入 CPL3。
- 建立独立的用户态代码页和用户栈映射。
- 用户代码页设置为 RX、U/S=1。
- 用户栈设置为 RW/NX、U/S=1。
- 内核身份映射保持 Supervisor-only。
- 用户态通过 `int 0x80` 进入 Ring 0 系统调用处理程序。
- 系统调用完成后成功返回 Ring 0。
- Ring 3 测试结束后，原有 Ring 0 Task A / Task B 调度器继续运行。

### 当前测试结果

| 测试项目 | 结果 |
|---|---|
| `make clean && make` | 通过 |
| `make test-memory` | 通过 |
| `make test-paging` | 通过（主机端逻辑测试） |
| `make test-boot` | 通过 |
| QEMU 内核启动 | 通过 |
| Ring 0 → Ring 3 → Ring 0 | 实际运行通过 |
| `int 0x80` 系统调用 | 实际运行通过 |
| Ring 0 Task A / Task B 调度 | 实际运行通过 |

### 尚未完成的安全验证

基础特权级切换通过，不代表所有用户态安全隔离测试已经完成。

下一阶段需要完成：

1. Ring 3 写入只读用户代码页时触发 Page Fault。
2. Ring 3 访问 Supervisor-only 内核页面时触发 Page Fault。
3. Ring 3 执行 NX 用户栈时触发 Page Fault。
4. 验证 Page Fault 的 CR2、错误码和异常处理路径。
5. 支持多个独立用户地址空间。
6. 完善系统调用 ABI。
7. 后续推进抢占式调度。

### 下一阶段开发方向

优先建立安全、可恢复的异常测试框架，再逐项验证用户代码页只读保护、内核页面访问保护和 NX 执行保护。

现有 `src/task.c`、`src/task.h` 和 `src/switch.S` 保留，Ring 3 开发不会以删除原有 Ring 0 调度器为代价。

详细验收记录见 [`RING3_VALIDATION_2026-11-10.md`](RING3_VALIDATION_2026-11-10.md)。
"""

block = start + "\n" + section.strip() + "\n" + end

pattern = re.compile(
    re.escape(start) + r".*?" + re.escape(end),
    re.DOTALL
)

if pattern.search(s):
    s = pattern.sub(lambda _: block, s, count=1)
else:
    s = s.rstrip() + "\n\n" + block + "\n"

# 删除明显过时的 Ring 3 禁用描述，避免 README 自相矛盾
obsolete = [
    r"(?m)^.*Ring 3 测试已禁用.*(?:\n|$)",
    r"(?m)^.*Ring3 测试暂时禁用.*(?:\n|$)",
    r"(?m)^.*Ring 3 功能尚未实现.*(?:\n|$)",
]

for pattern in obsolete:
    s = re.sub(pattern, "", s)

p.write_text(s.rstrip() + "\n", encoding="utf-8")
print("[OK] README.md 更新完成")
PY

# 3. 更新 Ring 3 验收报告
python3 - <<'PY'
from pathlib import Path
import re

p = Path("RING3_VALIDATION_2026-11-10.md")
s = p.read_text(encoding="utf-8")

start = "<!-- QMjyOS-QEMU-VALIDATION-START -->"
end = "<!-- QMjyOS-QEMU-VALIDATION-END -->"

section = r"""
## QEMU 实际运行验收补充（2026-11-10）

本节记录本次 QEMU 实际启动输出。主机端单元测试与 QEMU 硬件运行验收分别统计。

### QEMU 关键运行日志

```text
Hello, x86_64 kernel!
Physical page allocator: initialized from Multiboot mmap.
Physical page allocator self-test: PASS (alloc/free/double-free).
Paging manager self-test: PASS (inactive page tables).
GDT and TSS initialized.

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
[task A] running
[task B] running
