#!/usr/bin/env python3
from pathlib import Path
from datetime import datetime
import shutil
import re
import sys

ROOT = Path.cwd()
TARGET = ROOT / "drivers/block/ide_pio.c"
WORK = ROOT / "build/overall-repair-work"

def stop(message):
    print("[STOP]", message)
    raise SystemExit(1)

if not TARGET.is_file():
    stop("找不到 drivers/block/ide_pio.c")

original = TARGET.read_text(encoding="utf-8")
updated = original
changes = []

# 1. 增加专用命令完成等待函数。
helper = r'''
/*
 * Wait for an ATA command to complete.
 * Completion requires BSY=0, DRQ=0 and no device error.
 */
static int ide_wait_complete(void)
{
    qm_u32_t timeout;
    qm_u8_t status;

    for (timeout = 0; timeout < IDE_TIMEOUT; ++timeout) {
        status = ide_inb(IDE_STATUS);

        if (status == 0xFF || status == 0x00)
            return -1;

        if (status & IDE_STATUS_BSY)
            continue;

        if (status & (IDE_STATUS_ERR | IDE_STATUS_DF))
            return -1;

        if (!(status & IDE_STATUS_DRQ))
            return 0;
    }

    return -1;
}

'''

if "static int ide_wait_complete(void)" not in updated:
    marker = "static void ide_select(qm_u8_t slave)"
    if updated.count(marker) != 1:
        stop("无法唯一定位 ide_select，拒绝插入完成等待函数")
    updated = updated.replace(marker, helper + marker, 1)
    changes.append("新增 ide_wait_complete")
else:
    print("[PASS] 完成等待函数已经存在")

# 2. 读操作传输后必须等待命令完成。
read_old = '''        ide_insw(
            IDE_DATA,
            buffer,
            IDE_WORDS_PER_SECTOR
        );
    }

    return 0;
}'''

read_new = '''        ide_insw(
            IDE_DATA,
            buffer,
            IDE_WORDS_PER_SECTOR
        );

        if (ide_wait_complete() != 0)
            return -1;
    }

    return 0;
}'''

if read_new not in updated:
    if updated.count(read_old) == 1:
        updated = updated.replace(read_old, read_new, 1)
        changes.append("修复 READ 传输完成等待")
    elif "ide_wait_complete()" not in updated:
        stop("无法唯一定位 READ 传输代码")
    else:
        print("[PASS] READ 完成等待可能已经修复")
else:
    print("[PASS] READ 完成等待已经存在")

# 3. 写操作发送数据后，要求 DRQ 清零。
write_old = '''        if (ide_wait(0) != 0)
            return -1;
    } else {
        ide_insw('''

write_new = '''        if (ide_wait_complete() != 0)
            return -1;
    } else {
        ide_insw('''

if write_new not in updated:
    if updated.count(write_old) == 1:
        updated = updated.replace(write_old, write_new, 1)
        changes.append("修复 WRITE 完成等待")
    else:
        print("[WARN] WRITE 代码结构与预期不同，未自动修改")
else:
    print("[PASS] WRITE 完成等待已经存在")

# 4. FLUSH 命令完成后，要求 DRQ 清零。
flush_start = updated.find("static qm_status_t ide_flush(")
if flush_start < 0:
    stop("找不到 ide_flush")

flush_end = updated.find("\nstatic ", flush_start + 1)
if flush_end < 0:
    flush_end = len(updated)

flush = updated[flush_start:flush_end]

if "ide_outb(IDE_COMMAND, IDE_CMD_FLUSH);" not in flush:
    stop("FLUSH 函数结构无法识别")

if "ide_wait_complete()" not in flush:
    flush_new, n = re.subn(
        r"(ide_outb\(IDE_COMMAND,\s*IDE_CMD_FLUSH\);\s*)"
        r"if\s*\(\s*ide_wait\(0\)\s*!=\s*0\s*\)",
        r"\1if (ide_wait_complete() != 0)",
        flush,
        count=1
    )
    if n != 1:
        stop("无法唯一修复 FLUSH 完成等待")
    updated = updated[:flush_start] + flush_new + updated[flush_end:]
    changes.append("修复 FLUSH 完成等待")
else:
    print("[PASS] FLUSH 完成等待已经存在")

# 5. 读写 LBA 边界检查改为减法形式，避免 lba + count 溢出。
old_bounds = '''    if (lba > IDE_MAX_LBA28 ||
        (qm_u64_t)count > ide_devices[slave].sectors ||
        lba + count > ide_devices[slave].sectors ||
        lba + count > 0x10000000ULL)
        return (qm_status_t)-1;'''

new_bounds = '''    if (lba > IDE_MAX_LBA28 ||
        lba > (qm_u64_t)ide_devices[slave].sectors ||
        (qm_u64_t)count >
            (qm_u64_t)ide_devices[slave].sectors - lba ||
        lba > 0x10000000ULL ||
        (qm_u64_t)count > 0x10000000ULL - lba)
        return (qm_status_t)-1;'''

count_old = updated.count(old_bounds)
if count_old:
    if count_old != 2:
        stop("读写边界检查数量异常，预期恰好两处")
    updated = updated.replace(old_bounds, new_bounds)
    changes.append("读写 LBA 边界检查改为无加法溢出形式")
elif updated.count(new_bounds) == 2:
    print("[PASS] 读写 LBA 溢出保护已经存在")
else:
    stop("读写边界代码与已知版本不匹配，拒绝猜测修改")

# 6. 确认完成等待函数确实存在且关键调用齐全。
if "static int ide_wait_complete(void)" not in updated:
    stop("完成等待函数缺失")

if "ide_wait_complete()" not in updated:
    stop("没有发现完成等待调用")

if updated == original:
    print("\n[PASS] 当前文件已经包含本脚本涉及的修复")
    print("[INFO] 未修改文件，也没有创建新备份")
    raise SystemExit(0)

# 7. 所有检查通过后才备份并写入。
stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
backup = WORK / ("phase2-ide-" + stamp)

if backup.exists():
    stop("备份目录已存在：" + str(backup))

backup.mkdir(parents=True)
shutil.copy2(TARGET, backup / "ide_pio.c.before")

temp = TARGET.with_name(TARGET.name + ".repair-tmp")
if temp.exists():
    stop("临时文件已存在，拒绝覆盖：" + str(temp))

try:
    temp.write_text(updated, encoding="utf-8")
    # 写入前重新确认目标没有被外部修改。
    if TARGET.read_text(encoding="utf-8") != original:
        stop("目标文件在修复过程中发生变化")
    temp.replace(TARGET)
except Exception:
    if temp.exists():
        temp.unlink()
    shutil.copy2(backup / "ide_pio.c.before", TARGET)
    stop("写入失败，已尝试恢复原文件")

print("\n========== IDE PIO 修复结果 ==========")
for item in changes:
    print("[FIX]", item)

print("[BACKUP]", backup)
print("[PASS] 修复文件：drivers/block/ide_pio.c")
print("[INFO] 尚未进行编译或 QEMU 验证")
