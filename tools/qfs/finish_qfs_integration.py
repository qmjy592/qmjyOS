from pathlib import Path

# fs_init.c 位于 FS_RAMFS 模块中，并引用 QFS 描述符。
p = Path("config/modules.dep")
s = p.read_text()
old = "FS_RAMFS: FS_VFS MM_HEAP"
new = "FS_RAMFS: FS_VFS MM_HEAP FS_QFS"

if new not in s:
    if old not in s:
        raise SystemExit("找不到 FS_RAMFS 依赖定义")
    s = s.replace(old, new, 1)
    p.write_text(s)

# QBuild test 命令纳入 QFS 主机端持久化测试。
p = Path("tools/qbuild/qbuild")
s = p.read_text()

old = """        test-ramfs \\
        test-interfaces \\
        test-boot"""

new = """        test-ramfs \\
        test-qfs \\
        test-interfaces \\
        test-boot"""

if new not in s:
    if old not in s:
        raise SystemExit("找不到 QBuild 测试命令列表")
    s = s.replace(old, new, 1)
    p.write_text(s)

print("QFS dependency and QBuild test integration applied.")
