#!/usr/bin/env python3
from pathlib import Path
from datetime import datetime
import shutil
import sys

src = Path("src/paging.c")
if not src.is_file():
    sys.exit("错误：找不到 src/paging.c")

text = src.read_text()

if "promoted_original" in text:
    sys.exit("检测到回滚字段已存在，停止以避免重复修改。")

changes = [
(
'''    size_t created_count;
    uint64_t *created[3];
};''',
'''    size_t created_count;
    uint64_t *created[3];
    size_t promoted_count;
    uint64_t *promoted_entries[3];
    uint64_t promoted_original[3];
};'''
),
(
'''static void rollback_created(struct walk_path *path) {
    while (path->created_count != 0) {''',
'''static void rollback_created(struct walk_path *path) {
    while (path->promoted_count != 0) {
        size_t n = --path->promoted_count;
        *path->promoted_entries[n] = path->promoted_original[n];
    }

    while (path->created_count != 0) {'''
),
(
'''    path->created_count = 0;
    path->tables[0] = root_table;''',
'''    path->created_count = 0;
    path->promoted_count = 0;
    path->tables[0] = root_table;'''
),
(
'''            if ((flags & VM_USER) && !(entry & VM_USER)) {
                entry |= VM_USER;
                table[index] = entry;
            }''',
'''            if ((flags & VM_USER) && !(entry & VM_USER)) {
                if (path->promoted_count >= 3) {
                    return VM_ERR_CORRUPT;
                }
                size_t n = path->promoted_count++;
                path->promoted_entries[n] = &table[index];
                path->promoted_original[n] = entry;
                entry |= VM_USER;
                table[index] = entry;
            }'''
)
]

for old, new in changes:
    if text.count(old) != 1:
        sys.exit("补丁匹配失败，文件未写入。未匹配片段开头：" + repr(old[:100]))
    text = text.replace(old, new, 1)

backup = src.with_name(
    "paging.c.backup-" + datetime.now().strftime("%Y%m%d-%H%M%S")
)
shutil.copy2(src, backup)

src.write_text(text)
print("分页权限回滚修复已应用。")
print("备份文件：", backup)
print("尚未执行测试；按计划稍后统一验证。")
