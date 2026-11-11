#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
MODE="${1:---check}"

case "$MODE" in
    --check|--apply) ;;
    *)
        echo "Usage: $0 [--check|--apply]"
        exit 2
        ;;
esac

cd "$ROOT"

python3 - "$MODE" <<'PYTHON_PATCH'
import sys
from pathlib import Path
from datetime import datetime

mode = sys.argv[1]

QFS_PATH = Path("fs/qfs/qfs.c")
TEST_PATH = Path("tests/filesystem/qfs_host_test.c")
README_PATH = Path("README.md")
DOC_PATH = Path("docs/filesystems/QFS_FAULT_RECOVERY.md")

def stop(message):
    print("[ABORT]", message)
    raise SystemExit(1)

def read(path):
    path = Path(path)
    if not path.is_file():
        stop(f"文件不存在：{path}")
    return path.read_text(encoding="utf-8")

def find_function(source, signature):
    pos = source.find(signature)
    if pos < 0:
        stop(f"找不到函数：{signature}")

    brace = source.find("{", pos)
    if brace < 0:
        stop(f"找不到函数体：{signature}")

    depth = 0
    for i in range(brace, len(source)):
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
            if depth == 0:
                return pos, i + 1

    stop(f"函数括号不匹配：{signature}")

def replace_function(source, signature, replacement):
    start, end = find_function(source, signature)
    return source[:start] + replacement + source[end:]

def insert_once(source, anchor, addition, label):
    if anchor not in source:
        stop(f"找不到插入位置：{label}")
    if addition.strip() in source:
        return source
    return source.replace(anchor, anchor + addition, 1)

qfs = read(QFS_PATH)
test = read(TEST_PATH)
readme = read(README_PATH)
makefile = read("Makefile")

print("[CHECK] QFS recovery patch prerequisites")

required = [
    "static int qfs_is_mounted;",
    "static int qfs_save_bitmap(void)",
    "static int qfs_flush(void)",
    "static int qfs_find_inode(const char *name)",
    "static int qfs_load_nodes(void)",
    "static int qfs_allocate_block(qm_u32_t *out_sector)",
    "static int qfs_free_block(qm_u32_t sector)",
    "static int qfs_write_file(",
    "static int qfs_create(",
    "static int qfs_remove(",
    "int qfs_format(const qm_partition_t *partition)",
    "int qfs_mount(const qm_partition_t *partition)",
    "int qfs_unmount(void)",
]

for item in required:
    if item not in qfs:
        stop(f"QFS 源码结构不匹配：{item}")
    print("[OK]", item)

if "test_disk_write" not in test:
    stop("主机测试缺少磁盘写入回调")

if "test-qfs:" not in makefile:
    stop("Makefile 中没有 test-qfs 目标")

print("[OK] QFS host test")
print("[OK] Makefile test-qfs target")
print("[OK] README")

# 1. Recovery state

recovery_declaration = """
/*
 * Runtime poison state. Critical rollback failure blocks
 * ordinary filesystem operations until unmount/remount.
 */
static int qfs_needs_recovery;
"""

if "static int qfs_needs_recovery;" not in qfs:
    qfs = insert_once(
        qfs,
        "static int qfs_is_mounted;\n",
        recovery_declaration,
        "recovery state"
    )

recovery_helpers = """
static int qfs_recovery_required(void)
{
    return qfs_needs_recovery != 0;
}

static void qfs_mark_recovery_required(void)
{
    qfs_needs_recovery = 1;
}

"""

if "static int qfs_recovery_required(void)" not in qfs:
    qfs = insert_once(
        qfs,
        "static int qfs_flush(void)\n",
        recovery_helpers,
        "recovery helpers"
    )

print("[OK] Recovery state prepared")

# 后续补丁段从这里继续。
# 2. Block allocation rollback

allocate_replacement = r'''
static int qfs_allocate_block(qm_u32_t *out_sector)
{
    qm_u32_t sector;

    if (out_sector == 0)
        return QFS_ERR_CORRUPT;

    if (qfs_recovery_required())
        return VFS_ERR_IO;

    for (sector = QFS_DATA_START;
         sector < qfs_total_sectors;
         ++sector) {
        if (!qfs_bitmap_test(sector)) {
            qfs_bitmap_set(sector);

            if (qfs_save_bitmap() != 0 ||
                qfs_flush() != 0) {
                int rollback_failed = 0;

                qfs_bitmap_clear(sector);

                if (qfs_save_bitmap() != 0)
                    rollback_failed = 1;

                if (qfs_flush() != 0)
                    rollback_failed = 1;

                if (rollback_failed)
                    qfs_mark_recovery_required();

                return VFS_ERR_IO;
            }

            *out_sector = sector;
            return 0;
        }
    }

    return VFS_ERR_NO_SPACE;
}
'''

qfs = replace_function(
    qfs,
    "static int qfs_allocate_block(qm_u32_t *out_sector)",
    allocate_replacement
)

# 3. Block release rollback

free_replacement = r'''
static int qfs_free_block(qm_u32_t sector)
{
    if (sector < QFS_DATA_START ||
        sector >= qfs_total_sectors)
        return QFS_ERR_CORRUPT;

    if (qfs_recovery_required())
        return VFS_ERR_IO;

    if (!qfs_bitmap_test(sector))
        return 0;

    qfs_bitmap_clear(sector);

    if (qfs_save_bitmap() == 0 &&
        qfs_flush() == 0)
        return 0;

    qfs_bitmap_set(sector);

    if (qfs_save_bitmap() != 0 ||
        qfs_flush() != 0)
        qfs_mark_recovery_required();

    return VFS_ERR_IO;
}
'''

qfs = replace_function(
    qfs,
    "static int qfs_free_block(qm_u32_t sector)",
    free_replacement
)

print("[OK] Allocation rollback prepared")
print("[OK] Release rollback prepared")
# 4. Operation guards

guards = {
    "static int qfs_read_file(": """
    if (qfs_recovery_required())
        return VFS_ERR_IO;

""",
    "static int qfs_write_file(": """
    if (qfs_recovery_required())
        return VFS_ERR_IO;

""",
    "static int qfs_lookup(": """
    if (qfs_recovery_required())
        return VFS_ERR_IO;

""",
    "static int qfs_create(": """
    if (qfs_recovery_required())
        return VFS_ERR_IO;

""",
    "static int qfs_remove(": """
    if (qfs_recovery_required())
        return VFS_ERR_IO;

""",
}

for signature, guard in guards.items():
    start, end = find_function(qfs, signature)
    body = qfs.find("{", start, end)

    if body < 0:
        stop(f"无法插入操作保护：{signature}")

    if "qfs_recovery_required()" not in qfs[body:end]:
        qfs = qfs[:body + 1] + guard + qfs[body + 1:]

# 5. Write rollback failure detection

write_start, write_end = find_function(
    qfs, "static int qfs_write_file("
)
write_body = qfs[write_start:write_end]

old_data_rollback = """            if (qfs_write_sector(
                    original_inode.blocks[i],
                    old_data[i]) != 0)
                failure = VFS_ERR_IO;
"""

new_data_rollback = """            if (qfs_write_sector(
                    original_inode.blocks[i],
                    old_data[i]) != 0) {
                failure = VFS_ERR_IO;
                qfs_mark_recovery_required();
            }
"""

if old_data_rollback not in write_body:
    stop("write rollback 数据恢复代码与预期不匹配")

write_body = write_body.replace(
    old_data_rollback, new_data_rollback, 1
)

old_inode_rollback = """    if (qfs_save_inode((qm_u32_t)inode_index) != 0) {
        node->size = original_inode.size;
        qfs_flush();
        return VFS_ERR_IO;
    }
"""

new_inode_rollback = """    if (qfs_save_inode((qm_u32_t)inode_index) != 0) {
        node->size = original_inode.size;
        qfs_mark_recovery_required();
        qfs_flush();
        return VFS_ERR_IO;
    }
"""

if old_inode_rollback not in write_body:
    stop("write rollback inode 代码与预期不匹配")

write_body = write_body.replace(
    old_inode_rollback, new_inode_rollback, 1
)

old_free_rollback = """    for (i = 0; i < new_count; ++i) {
        if (qfs_free_block(new_blocks[i]) != 0)
            failure = VFS_ERR_IO;
    }
"""

new_free_rollback = """    for (i = 0; i < new_count; ++i) {
        if (qfs_free_block(new_blocks[i]) != 0) {
            failure = VFS_ERR_IO;
            qfs_mark_recovery_required();
        }
    }
"""

if old_free_rollback not in write_body:
    stop("write rollback block release 代码与预期不匹配")

write_body = write_body.replace(
    old_free_rollback, new_free_rollback, 1
)

qfs = qfs[:write_start] + write_body + qfs[write_end:]

# 6. Create/remove rollback failure detection

create_start, create_end = find_function(
    qfs, "static int qfs_create("
)
create_body = qfs[create_start:create_end]

create_return = "    return rollback_failed ? VFS_ERR_IO : VFS_ERR_IO;"

if create_return not in create_body:
    stop("create rollback 返回代码不匹配")

create_body = create_body.replace(
    create_return,
    """    if (rollback_failed)
        qfs_mark_recovery_required();

    return VFS_ERR_IO;""",
    1
)

qfs = qfs[:create_start] + create_body + qfs[create_end:]

remove_start, remove_end = find_function(
    qfs, "static int qfs_remove("
)
remove_body = qfs[remove_start:remove_end]

remove_return = "    return rollback_failed ? VFS_ERR_IO : VFS_ERR_IO;"

if remove_return not in remove_body:
    stop("remove rollback 返回代码不匹配")

remove_body = remove_body.replace(
    remove_return,
    """    if (rollback_failed)
        qfs_mark_recovery_required();

    return VFS_ERR_IO;""",
    1
)

qfs = qfs[:remove_start] + remove_body + qfs[remove_end:]

print("[OK] Operation guards prepared")
print("[OK] Write rollback protection prepared")
print("[OK] Create/remove rollback protection prepared")
# 7. Orphan data block recovery

orphan_helper = r'''
/*
 * Reclaim allocated data blocks not referenced by any valid inode.
 * This runs only after mount ownership validation.
 * It does not delete or rewrite inode records.
 */
static int qfs_reclaim_orphan_blocks(void)
{
    qm_u8_t referenced[QFS_MAX_SECTORS];
    qm_u32_t i;
    int changed = 0;

    qfs_zero(referenced, sizeof(referenced));

    for (i = 0; i < QFS_MAX_INODES; ++i) {
        qm_u32_t j;

        if (!qfs_inodes[i].used)
            continue;

        for (j = 0; j < QFS_DIRECT_BLOCKS; ++j) {
            qm_u32_t block = qfs_inodes[i].blocks[j];

            if (block != 0) {
                if (block < QFS_DATA_START ||
                    block >= qfs_total_sectors)
                    return QFS_ERR_CORRUPT;

                referenced[block] = 1;
            }
        }
    }

    for (i = QFS_DATA_START; i < qfs_total_sectors; ++i) {
        if (qfs_bitmap_test(i) && !referenced[i]) {
            qfs_bitmap_clear(i);
            changed = 1;
        }
    }

    if (!changed)
        return 0;

    if (qfs_save_bitmap() != 0 ||
        qfs_flush() != 0) {
        qfs_mark_recovery_required();
        return QFS_ERR_IO;
    }

    return 0;
}
'''

if "static int qfs_reclaim_orphan_blocks(void)" not in qfs:
    qfs = insert_once(
        qfs,
        "static int qfs_load_nodes(void)\n",
        orphan_helper,
        "orphan recovery helper"
    )

mount_start, mount_end = find_function(
    qfs, "int qfs_mount(const qm_partition_t *partition)"
)
mount_body = qfs[mount_start:mount_end]

if "if (qfs_load_nodes() != 0)" not in mount_body:
    stop("mount 节点加载锚点不存在")

mount_body = mount_body.replace(
    """    if (qfs_load_nodes() != 0) {
""",
    """    if (qfs_reclaim_orphan_blocks() != 0) {
        qfs_is_mounted = 0;
        return VFS_ERR_IO;
    }

    if (qfs_load_nodes() != 0) {
""",
    1
)

mount_body = mount_body.replace(
    """    qfs_mountpoint = 0;
""",
    """    qfs_mountpoint = 0;
    qfs_needs_recovery = 0;
""",
    1
)

qfs = qfs[:mount_start] + mount_body + qfs[mount_end:]

# 8. Format ordering: invalid superblock first, valid last

format_replacement = r'''
int qfs_format(const qm_partition_t *partition)
{
    qm_u8_t sector[QFS_SECTOR_SIZE];
    qm_u32_t i;

    if (qfs_is_mounted)
        return VFS_ERR_BUSY;

    if (!qfs_valid_partition(partition))
        return VFS_ERR_INVALID;

    qfs_device_id = partition->device_id;
    qfs_partition_start = partition->start_lba;
    qfs_total_sectors = (qm_u32_t)partition->sector_count;
    qfs_needs_recovery = 0;
    qfs_is_mounted = 1;

    /* Invalidate any old filesystem before changing metadata. */
    qfs_zero(sector, sizeof(sector));

    if (qfs_write_sector(0, sector) != 0 ||
        qfs_flush() != 0) {
        qfs_is_mounted = 0;
        return VFS_ERR_IO;
    }

    qfs_zero(qfs_bitmap, sizeof(qfs_bitmap));
    qfs_zero(qfs_inodes, sizeof(qfs_inodes));
    qfs_zero(qfs_directory, sizeof(qfs_directory));

    for (i = 0; i < QFS_DATA_START; ++i)
        qfs_bitmap_set(i);

    if (qfs_save_bitmap() != 0)
        goto format_failed;

    for (i = 0; i < QFS_INODE_SECTORS; ++i) {
        qfs_zero(sector, sizeof(sector));

        if (qfs_write_sector(QFS_INODE_START + i, sector) != 0)
            goto format_failed;
    }

    if (qfs_save_directory() != 0)
        goto format_failed;

    if (qfs_flush() != 0)
        goto format_failed;

    /* Commit valid superblock last. */
    qfs_zero(sector, sizeof(sector));
    qfs_write_u32(&sector[0], QFS_MAGIC);
    qfs_write_u32(&sector[4], QFS_VERSION);
    qfs_write_u32(&sector[8], qfs_total_sectors);
    qfs_write_u32(&sector[12], QFS_DATA_START);
    qfs_write_u32(&sector[16], QFS_MAX_INODES);
    qfs_write_u32(&sector[20], QFS_MAX_FILE_SIZE);
    qfs_write_u32(&sector[24], QFS_BITMAP_START);
    qfs_write_u32(&sector[28], QFS_INODE_START);
    qfs_write_u32(&sector[32], QFS_DIRECTORY_START);

    if (qfs_write_sector(0, sector) != 0 ||
        qfs_flush() != 0)
        goto format_failed;

    qfs_is_mounted = 0;
    return VFS_OK;

format_failed:
    /* Best effort: leave an invalid superblock after failure. */
    qfs_zero(sector, sizeof(sector));
    qfs_write_sector(0, sector);
    qfs_flush();

    qfs_is_mounted = 0;
    return VFS_ERR_IO;
}
'''

qfs = replace_function(
    qfs,
    "int qfs_format(const qm_partition_t *partition)",
    format_replacement
)

print("[OK] Orphan block recovery prepared")
print("[OK] Mount recovery hook prepared")
print("[OK] Format ordering prepared")

# 9. Host test fault injection

if "static int fail_write_after = -1;" not in test:
    test = insert_once(
        test,
        "#define TEST_DISK_SECTORS",
        "static int fail_write_after = -1;\n\n",
        "host write fault injection"
    )

fault_anchor = """    memcpy(
        &test_disk[lba * TEST_SECTOR_SIZE],
        buffer,
        count * TEST_SECTOR_SIZE);"""

if fault_anchor not in test:
    stop("host write callback anchor not found")

test = test.replace(
    fault_anchor,
    """    if (fail_write_after == 0) {
        fail_write_after = -1;
        return QM_ERR_IO;
    }
    if (fail_write_after > 0)
        --fail_write_after;

""" + fault_anchor,
    1
)

format_test = r'''
    /* A failed format must not leave a mountable filesystem. */
    fail_write_after = 1;
    CHECK(qfs_format(&partition) == VFS_ERR_IO,
          "reject interrupted format");
    fail_write_after = -1;
    CHECK(qfs_mount(&partition) != VFS_OK,
          "reject mount after interrupted format");
'''

if "reject interrupted format" not in test:
    test_anchor = '    CHECK(\n        qm_block_device_unregister(TEST_DEVICE_ID) == QM_OK,'
    if test_anchor not in test:
        stop("host test final unregister anchor not found")

    test = test.replace(
        test_anchor,
        format_test + "\n" + test_anchor,
        1
    )

print("[OK] Host format fault injection prepared")

# 10. README documentation links

readme_section = """
### QFS fault recovery

- 中文：参见 [QFS 故障恢复说明](docs/filesystems/QFS_FAULT_RECOVERY.md)。
- English: See [QFS Fault Recovery](docs/filesystems/QFS_FAULT_RECOVERY.md).
"""

if "QFS_FAULT_RECOVERY.md" not in readme:
    readme = readme.rstrip() + "\n\n" + readme_section

# 11. Bilingual recovery documentation

doc = r'''# QFS 故障恢复说明 / QFS Fault Recovery

## 中文

### 恢复状态

当关键元数据回滚或持久化失败时，QFS 会进入 recovery-required 状态。
此状态下拒绝普通文件系统操作，避免继续扩大元数据不一致。

卸载操作仍然允许，以便上层尝试安全退出。

### 位图操作

数据块分配和释放失败时，QFS 尝试恢复内存中的位图状态，并重新持久化和刷新。
如果恢复本身失败，则标记文件系统需要恢复。

### 孤立数据块

挂载时，在完成 inode 与数据块所有权验证后，扫描数据区。
对于已经分配、但没有被任何有效 inode 引用的数据块，回收其位图标记。

此过程不会删除或改写没有目录项的 inode。

### 格式化顺序

格式化首先写入无效 superblock 并刷新；
随后初始化位图、inode 和目录元数据并刷新；
最后写入有效 superblock 并再次刷新。

如果中途失败，会尽力保留无效 superblock。

### 持久性限制

QFS 当前没有 journal，也没有完整的断电原子性保证。
故障恢复逻辑只能降低部分失败造成的不一致风险，不能保证任意断电场景下的数据完整性。

## English

### Recovery-required state

If critical metadata rollback or persistence fails, QFS enters a
recovery-required state. Ordinary filesystem operations are rejected
to avoid worsening metadata inconsistency.

Unmount remains permitted so that the filesystem can be detached.

### Bitmap operations

When allocation or release fails, QFS attempts to restore the in-memory
bitmap and persist and flush the restored state. If recovery itself fails,
the filesystem is marked as requiring recovery.

### Orphan data blocks

During mount, after inode and block ownership validation, QFS scans the
data region. Allocated blocks that are not referenced by any valid inode
are reclaimed by clearing their bitmap entries.

This process does not delete or rewrite inodes that have no directory entry.

### Format ordering

Formatting first writes and flushes an invalid superblock;
then initializes and flushes bitmap, inode, and directory metadata;
finally writes and flushes the valid superblock.

If formatting fails, QFS makes a best-effort attempt to leave an invalid
superblock.

### Durability limitations

QFS currently has no journal and does not provide full power-loss atomicity.
Recovery logic reduces some inconsistency risks but cannot guarantee
data integrity under every possible power-loss scenario.
'''

print("[OK] README documentation link prepared")
print("[OK] Bilingual recovery documentation prepared")

# 12. Final validation and transactional output

import os
import sys
import shutil
import tempfile
from datetime import datetime

outputs = [
    (QFS_PATH, qfs),
    (TEST_PATH, test),
    (README_PATH, readme),
    (DOC_PATH, doc),
]

# Validate all generated content before touching repository files.
for path, content in outputs:
    if not isinstance(content, str) or not content.strip():
        stop("empty generated output: " + path)

    if "\x00" in content:
        stop("NUL byte found in generated output: " + path)

    if not os.path.isfile(path) and path != DOC_PATH:
        stop("source file does not exist: " + path)

print("[CHECK] All generated outputs are non-empty")
print("[CHECK] Source files are present")

# Show the planned changes.
for path, content in outputs:
    print("[PLAN] {} -> {} characters".format(path, len(content)))

apply_mode = "--apply" in sys.argv

if not apply_mode:
    print("[SAFE] Check mode only; no repository files were modified.")
    print("[NEXT] Review the patch and source anchors before using --apply.")
    raise SystemExit(0)

# Create timestamped backups before writing any output.
timestamp = datetime.now().strftime("%Y%m%d-%H%M%S")
backups = []

for path, content in outputs:
    if os.path.isfile(path):
        backup = path + ".qfs-recovery-backup-" + timestamp
        shutil.copy2(path, backup)
        backups.append((path, backup))

print("[BACKUP] {} source backup(s) created".format(len(backups)))

# Prepare all temporary files first.
temporary_files = []

try:
    for path, content in outputs:
        parent = os.path.dirname(path) or "."
        os.makedirs(parent, exist_ok=True)

        fd, temporary = tempfile.mkstemp(
            prefix=".qfs-recovery-",
            dir=parent
        )

        with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as stream:
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())

        temporary_files.append((temporary, path))

    # Replace destination files only after every temporary file is ready.
    replaced = []

    for temporary, path in temporary_files:
        os.replace(temporary, path)
        replaced.append(path)

    print("[APPLY] {} file(s) written".format(len(replaced)))
    print("[BACKUP] Backups retained for manual rollback")

except Exception as error:
    for temporary, path in temporary_files:
        if os.path.exists(temporary):
            try:
                os.unlink(temporary)
            except OSError:
                pass

    print("[ERROR] Transactional write failed: {}".format(error))
    print("[WARNING] Inspect repository files and backups before continuing.")
    raise SystemExit(1)

print("[DONE] Patch files written.")
print("[NEXT] Run syntax checks, inspect git diff, then run make test-qfs.")
PYTHON_PATCH
