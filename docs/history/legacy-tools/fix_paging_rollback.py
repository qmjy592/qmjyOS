#!/usr/bin/env python3

from pathlib import Path
import sys

source = Path("src/paging.c")

if not source.is_file():
    sys.exit("错误：找不到 src/paging.c")

text = source.read_text()

required = [
    "struct walk_path {",
    "static void rollback_created(struct walk_path *path)",
    "static int walk(uint64_t va, int create, uint64_t flags,",
    "int vm_map_page(uint64_t va, uint64_t pa, uint64_t flags)",
]

for item in required:
    if item not in text:
        sys.exit(f"错误：源文件缺少预期代码：{item}")

if "promoted_original" in text:
    sys.exit("检测到权限回滚字段已存在；拒绝重复应用。")

print("检查通过：找到了预期的分页管理器代码。")
print("脚本已创建，但尚未修改 src/paging.c。")
print("下一步将应用事务回滚修复。")
