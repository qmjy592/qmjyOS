from pathlib import Path

def replace_once(path, old, new):
    p = Path(path)
    s = p.read_text()
    if new in s:
        return
    if old not in s:
        raise SystemExit(f"无法定位修改位置：{path}")
    p.write_text(s.replace(old, new, 1))

# 公共块设备接口。
p = Path("include/qmjy/block.h")
s = p.read_text()
decl = """
qm_status_t qm_block_flush(qm_block_device_id_t id);

qm_status_t qm_block_get_info(
    qm_block_device_id_t id,
    qm_block_device_info_t *out_info
);
"""
if "qm_block_get_info(" not in s:
    s = s.replace("\n#endif", decl + "\n#endif")
p.write_text(s)

# 模块配置。
replace_once(
    "config/modules.conf",
    "DRIVER_BLOCK=optional",
    "DRIVER_BLOCK=enabled"
)

p = Path("config/modules.conf")
s = p.read_text()
if "FS_QFS=" not in s:
    s = s.replace(
        "FS_RAMFS=enabled",
        "FS_RAMFS=enabled\nFS_QFS=enabled"
    )
p.write_text(s)

# 模块依赖。
p = Path("config/modules.dep")
s = p.read_text()
if "\nFS_QFS:" not in s:
    s = s.replace(
        "FS_RAMFS: FS_VFS MM_HEAP",
        "FS_RAMFS: FS_VFS MM_HEAP\nFS_QFS: FS_VFS DRIVER_BLOCK"
    )
p.write_text(s)

# 模块状态。
p = Path("config/modules.status")
s = p.read_text()
if "\nFS_QFS=" not in s:
    s = s.replace(
        "FS_RAMFS=experimental",
        "FS_RAMFS=experimental\nFS_QFS=experimental"
    )
p.write_text(s)

# QBuild 源码映射。
p = Path("tools/qbuild/module_plan.py")
s = p.read_text()

driver_block = '''    "DRIVER_BLOCK": [
        ("drivers/block/block.c", "drivers/block/block.o"),
        ("drivers/block/ide_pio.c", "drivers/block/ide_pio.o"),
    ],
'''

if '"DRIVER_BLOCK": [' not in s:
    s = s.replace(
        '    "DRIVER_SERIAL": [',
        driver_block + '    "DRIVER_SERIAL": ['
    )

qfs_module = '''    "FS_QFS": [
        ("fs/partition/mbr.c", "fs/partition/mbr.o"),
        ("fs/qfs/qfs.c", "fs/qfs/qfs.o"),
    ],
'''

if '"FS_QFS": [' not in s:
    s = s.replace(
        '    "FS_RAMFS": [',
        qfs_module + '    "FS_RAMFS": ['
    )

p.write_text(s)

# Makefile：QFS 主机端回归测试。
p = Path("Makefile")
s = p.read_text()

if "HOST_QFS_TEST :=" not in s:
    s = s.replace(
        "HOST_RAMFS_TEST := .ramfs-host-test",
        "HOST_RAMFS_TEST := .ramfs-host-test\nHOST_QFS_TEST := .qfs-host-test"
    )

if "test-qfs" not in s:
    s = s.replace(
        "test-memory test-paging test-vfs test-ramfs test-boot",
        "test-memory test-paging test-vfs test-ramfs test-qfs test-boot"
    )

if "$(HOST_QFS_TEST)" not in s:
    s = s.replace(
        "rm -f $(HOST_VFS_TEST) $(HOST_RAMFS_TEST)",
        "rm -f $(HOST_VFS_TEST) $(HOST_RAMFS_TEST) $(HOST_QFS_TEST)",
        1
    )

if "test-qfs: $(HOST_QFS_TEST)" not in s:
    anchor = """test-ramfs: $(HOST_RAMFS_TEST)
\t./$(HOST_RAMFS_TEST)
"""
    addition = anchor + """
test-qfs: $(HOST_QFS_TEST)
\t./$(HOST_QFS_TEST)

$(HOST_QFS_TEST): tests/filesystem/qfs_host_test.c fs/qfs/qfs.c fs/vfs/vfs.c drivers/block/block.c
\t$(HOST_CC) $(HOST_TEST_CFLAGS) -I. -Iinclude -o $@ tests/filesystem/qfs_host_test.c fs/qfs/qfs.c fs/vfs/vfs.c drivers/block/block.c
"""
    if anchor not in s:
        raise SystemExit("Makefile 中找不到 test-ramfs 目标")
    s = s.replace(anchor, addition, 1)

p.write_text(s)

print("QFS build integration applied.")
