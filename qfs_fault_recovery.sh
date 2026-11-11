#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

# 安全默认值：不指定参数时只检查。
MODE="${1:---check}"

if [[ "$MODE" != "--check" && "$MODE" != "--apply" ]]; then
    echo "用法: $0 [--check|--apply]"
    exit 2
fi

python3 - "$MODE" <<'PY'
from pathlib import Path
import re
import sys

mode = sys.argv[1]
root = Path.cwd()

required = {
    "fs/qfs/qfs.c": [
        "qfs_allocate_block",
        "qfs_free_block",
        "qfs_write_file",
        "qfs_lookup",
        "qfs_create",
        "qfs_remove",
        "qfs_format",
        "qfs_mount",
        "qfs_unmount",
    ],
    "tests/filesystem/qfs_host_test.c": [
        "test_disk_write",
        "test_disk_flush",
        "int main(void)",
    ],
    "Makefile": [
        "test-qfs:",
        "$(HOST_QFS_TEST)",
    ],
    "README.md": [
        "Filesystems and interfaces",
        "test-ramfs",
    ],
}

def stop(message):
    print("[STOP] " + message, file=sys.stderr)
    raise SystemExit(1)

def read_file(name):
    path = root / name
    if not path.is_file():
        stop("缺少文件: " + name)
    try:
        return path.read_text(encoding="utf-8")
    except Exception as exc:
        stop(f"读取失败 {name}: {exc}")

def function_body(source, name):
    # 只匹配函数定义，不匹配注释和前置声明。
    pattern = re.compile(
        r"(?m)^[ \t]*(?:static[ \t]+)?"
        r"(?:int|void|qm_status_t|struct[ \t]+vfs_node[ \t]*\*)"
        r"[ \t]+" + re.escape(name) + r"[ \t]*\("
    )

    matches = list(pattern.finditer(source))
    if len(matches) != 1:
        stop(
            f"{name}: 函数定义应为 1 处，实际为 {len(matches)} 处"
        )

    start = matches[0].start()
    brace = source.find("{", matches[0].end())

    if brace < 0:
        stop(name + ": 找不到函数体")

    # 跳过注释、字符串和字符常量，避免误计数花括号。
    i = brace
    depth = 0
    state = "code"

    while i < len(source):
        ch = source[i]
        nxt = source[i + 1] if i + 1 < len(source) else ""

        if state == "code":
            if ch == "/" and nxt == "*":
                state = "comment"
                i += 2
                continue
            if ch == "/" and nxt == "/":
                state = "line_comment"
                i += 2
                continue
            if ch == '"':
                state = "string"
                i += 1
                continue
            if ch == "'":
                state = "char"
                i += 1
                continue
            if ch == "{":
                depth += 1
            elif ch == "}":
                depth -= 1
                if depth == 0:
                    return source[start:i + 1]
        elif state == "comment":
            if ch == "*" and nxt == "/":
                state = "code"
                i += 2
                continue
        elif state == "line_comment":
            if ch == "\n":
                state = "code"
        elif state in ("string", "char"):
            if ch == "\\":
                i += 2
                continue
            if state == "string" and ch == '"':
                state = "code"
            elif state == "char" and ch == "'":
                state = "code"

        i += 1

    stop(name + ": 函数花括号不匹配")

sources = {}

for name, anchors in required.items():
    source = read_file(name)
    sources[name] = source

    for anchor in anchors:
        if anchor not in source:
            stop(f"{name}: 缺少必要锚点: {anchor}")

qfs = sources["fs/qfs/qfs.c"]

print("[CHECK] QFS 函数定义")

function_names = required["fs/qfs/qfs.c"]
for name in function_names:
    body = function_body(qfs, name)
    print(f"  [OK] {name}: {len(body)} 字符")

print("[CHECK] QFS 故障恢复现状")

checks = {
    "recovery flag": "qfs_needs_recovery",
    "allocation rollback persistence": "qfs_save_bitmap()",
    "format superblock invalidation": "QFS_MAGIC",
    "mount ownership validation": "block_seen",
    "write rollback": "original_inode",
    "create rollback": "rollback_failed",
    "remove rollback": "rollback_failed",
}

for label, token in checks.items():
    present = token in qfs
    print(
        f"  [{'FOUND' if present else 'MISSING'}] {label}"
    )

print("[CHECK] Host test and Makefile")

test_source = sources["tests/filesystem/qfs_host_test.c"]
makefile = sources["Makefile"]
readme = sources["README.md"]

for label, condition in [
    ("host block write callback", "test_disk_write" in test_source),
    ("host flush callback", "test_disk_flush" in test_source),
    ("QFS host test target", "test-qfs:" in makefile),
    ("QFS test build rule", "$(HOST_QFS_TEST)" in makefile),
    ("README filesystem section", "Filesystems and interfaces" in readme),
]:
    print(f"  [{'OK' if condition else 'MISSING'}] {label}")

if mode == "--check":
    print()
    print("[SAFE] 只检查模式，没有修改任何文件。")
    print("[NEXT] 检查结果已输出；尚未应用故障恢复补丁。")
    raise SystemExit(0)

stop(
    "--apply 暂不开放：当前版本是安全审计器，"
    "不会把未经逐项验证的补丁写入源码。"
)
PY
