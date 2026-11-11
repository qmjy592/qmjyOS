#!/usr/bin/env python3
from pathlib import Path
from datetime import datetime
import re
import shutil
import os
import sys

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "drivers/block/ide_pio.c"
HEADER = ROOT / "include/qmjy/block.h"


def stop(message):
    print("[STOP]", message)
    raise SystemExit(1)


def body_bounds(source, marker):
    pos = source.find(marker)
    if pos < 0:
        stop("找不到函数：" + marker)

    opening = source.find("{", pos)
    if opening < 0:
        stop("函数缺少左大括号：" + marker)

    depth = 0
    state = "code"
    i = opening

    while i < len(source):
        ch = source[i]
        nxt = source[i + 1] if i + 1 < len(source) else ""

        if state == "line":
            if ch == "\n":
                state = "code"
            i += 1
            continue

        if state == "block":
            if ch == "*" and nxt == "/":
                state = "code"
                i += 2
            else:
                i += 1
            continue

        if state == "string" or state == "char":
            if ch == "\\":
                i += 2
                continue
            if (state == "string" and ch == '"') or \
               (state == "char" and ch == "'"):
                state = "code"
            i += 1
            continue

        if ch == "/" and nxt == "/":
            state = "line"
            i += 2
            continue
        if ch == "/" and nxt == "*":
            state = "block"
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
                return opening, i

        i += 1

    stop("函数大括号不匹配：" + marker)


def get_body(source, marker):
    opening, closing = body_bounds(source, marker)
    return source[opening + 1:closing]


def replace_body(source, marker, new_body):
    opening, closing = body_bounds(source, marker)
    return (
        source[:opening + 1]
        + "\n"
        + new_body.strip()
        + "\n"
        + source[closing:]
    )


def require(condition, message):
    if not condition:
        stop(message)


def build_update(source):
    require(SOURCE.is_file(), "IDE 源文件不存在")
    require(HEADER.is_file(), "Block 头文件不存在")

    header = HEADER.read_text(encoding="utf-8")

    require("qm_block_get_info" in header,
            "Block 查询接口不存在")
    require("qm_block_device_unregister" in header,
            "Block 注销接口不存在")
    require("qm_block_device_register" in header,
            "Block 注册接口不存在")

    require("static int ide_wait_complete(" in source,
            "缺少 ide_wait_complete，拒绝继续")
    require("static const qm_block_device_ops_t ide_block_ops" in source,
            "缺少 IDE Block 操作表")

    # IDENTIFY 数据传输结束后必须等待 ATA 命令完成。
    identify_io = re.search(
        r"ide_insw\s*\(\s*IDE_DATA\s*,\s*identify\s*,"
        r"\s*IDE_WORDS_PER_SECTOR\s*\)\s*;",
        source
    )
    require(identify_io is not None,
            "无法定位 IDENTIFY 数据读取位置")

    after_io = source[identify_io.end():identify_io.end() + 180]
    if "ide_wait_complete()" not in after_io:
        source = (
            source[:identify_io.end()]
            + "\n\n    if (ide_wait_complete() != 0)\n"
              "        return -1;\n"
            + source[identify_io.end():]
        )

    # 设备名称校验：不能误注销其他驱动注册的设备。
    helper = r'''
static int ide_name_equal(const char *a, const char *b)
{
    if (a == 0 || b == 0)
        return 0;

    while (*a != '\0' && *b != '\0') {
        if (*a != *b)
            return 0;
        ++a;
        ++b;
    }

    return *a == *b;
}
'''
    register_marker = "static qm_status_t ide_register_disk("
    require(register_marker in source,
            "找不到 ide_register_disk")

    if "static int ide_name_equal(" not in source:
        pos = source.find(register_marker)
        source = source[:pos] + helper + "\n" + source[pos:]

    register_body = r'''
    qm_block_device_info_t info;
    qm_block_device_info_t old_info;
    qm_block_device_id_t id;
    const char *name;
    qm_status_t rc;

    if (slave == IDE_DEVICE_MASTER) {
        id = QM_IDE_PRIMARY_MASTER;
        name = "ide-primary-master";
    } else if (slave == IDE_DEVICE_SLAVE) {
        id = QM_IDE_PRIMARY_SLAVE;
        name = "ide-primary-slave";
    } else {
        return (qm_status_t)-1;
    }

    info.id = id;
    info.block_count = sectors;
    info.block_size = IDE_SECTOR_SIZE;
    info.flags = 0;
    info.name = name;

    /*
     * IDE IDs are reserved for this driver.
     * An existing entry with another name must never be removed.
     */
    rc = qm_block_get_info(id, &old_info);

    if (rc == 0) {
        if (!ide_name_equal(old_info.name, name))
            return (qm_status_t)-2;

        if (old_info.block_count == (qm_u64_t)sectors &&
            old_info.block_size == IDE_SECTOR_SIZE &&
            old_info.flags == 0)
            return (qm_status_t)0;

        /*
         * Capacity changed. Replace the old registration.
         * Restore the previous metadata if the new registration fails.
         */
        if (qm_block_device_unregister(id) != 0)
            return (qm_status_t)-3;

        rc = qm_block_device_register(&info, &ide_block_ops);
        if (rc == 0)
            return (qm_status_t)0;

        if (qm_block_device_register(&old_info, &ide_block_ops) != 0)
            return (qm_status_t)-4;

        return (qm_status_t)-5;
    }

    return qm_block_device_register(&info, &ide_block_ops);
'''

    source = replace_body(
        source,
        register_marker,
        register_body
    )

    init_marker = "qm_status_t qm_ide_pio_init(void)"
    require(init_marker in source,
            "找不到 qm_ide_pio_init")

    init_body = r'''
    qm_u32_t master_sectors = 0;
    qm_u32_t slave_sectors = 0;
    int master_found;
    int slave_found;
    int registered = 0;

    /* This driver uses polling, not IDE channel interrupts. */
    ide_outb(IDE_CONTROL, 0x02);

    /*
     * Probe both devices before changing their published state.
     * A failed probe does not erase a previously working registration.
     */
    master_found = ide_identify(
        IDE_DEVICE_MASTER,
        &master_sectors
    );

    slave_found = ide_identify(
        IDE_DEVICE_SLAVE,
        &slave_sectors
    );

    if (master_found == 0 &&
        ide_register_disk(IDE_DEVICE_MASTER, master_sectors) == 0) {
        ide_devices[IDE_DEVICE_MASTER].sectors = master_sectors;
        ide_devices[IDE_DEVICE_MASTER].present = 1;
        ++registered;
    }

    if (slave_found == 0 &&
        ide_register_disk(IDE_DEVICE_SLAVE, slave_sectors) == 0) {
        ide_devices[IDE_DEVICE_SLAVE].sectors = slave_sectors;
        ide_devices[IDE_DEVICE_SLAVE].present = 1;
        ++registered;
    }

    /*
     * Do not clear old state merely because a later probe timed out.
     * A transient probe failure must not silently disable a registered disk.
     */
    if (registered != 0 ||
        ide_devices[IDE_DEVICE_MASTER].present ||
        ide_devices[IDE_DEVICE_SLAVE].present)
        return (qm_status_t)0;

    return (qm_status_t)-1;
'''

    source = replace_body(source, init_marker, init_body)
    return source


def validate(source):
    require("static int ide_name_equal(" in source,
            "设备名称校验函数缺失")

    identify_body = get_body(source, "static int ide_identify(")
    require(
        re.search(
            r"ide_insw\s*\(\s*IDE_DATA\s*,\s*identify\s*,"
            r"\s*IDE_WORDS_PER_SECTOR\s*\)\s*;"
            r"\s*if\s*\(\s*ide_wait_complete\(\)",
            identify_body
        ) is not None,
        "IDENTIFY 完成等待验证失败"
    )

    transfer = get_body(source, "static int ide_transfer_one(")
    require(transfer.count("ide_wait_complete()") >= 2,
            "READ/WRITE 完成等待不完整")

    read_body = get_body(source, "static qm_status_t ide_read(")
    write_body = get_body(source, "static qm_status_t ide_write(")

    for label, body in (("READ", read_body), ("WRITE", write_body)):
        require("0x10000000ULL - lba" in body,
                label + " 缺少 LBA28 减法边界检查")
        require("ide_devices[slave].sectors - lba" in body,
                label + " 缺少设备容量减法边界检查")

    flush_body = get_body(source, "static qm_status_t ide_flush(")
    require(
        flush_body.find("ide_select(slave)") <
        flush_body.find("ide_wait(0)"),
        "FLUSH 设备选择顺序异常"
    )
    require("ide_wait_complete()" in flush_body,
            "FLUSH 缺少完成等待")

    register_body = get_body(source, "static qm_status_t ide_register_disk(")
    require("qm_block_get_info" in register_body,
            "IDE 注册缺少重复设备查询")
    require("qm_block_device_unregister" in register_body,
            "IDE 容量更新缺少旧注册处理")
    require("ide_name_equal" in register_body,
            "IDE 注册缺少设备归属检查")

    init_body = get_body(source, "qm_status_t qm_ide_pio_init(void)")
    require(init_body.count("ide_identify(") == 2,
            "初始化必须探测主盘和从盘")
    require("present = 0" not in init_body,
            "初始化仍会无条件清空设备状态")
    require("ide_register_disk" in init_body,
            "初始化缺少设备注册")

    return True


def main():
    if not SOURCE.is_file():
        stop("找不到 drivers/block/ide_pio.c")

    original = SOURCE.read_text(encoding="utf-8")
    updated = build_update(original)
    validate(updated)

    if updated == original:
        print("[PASS] IDE PIO 已符合本批修复要求，无需重复修改")
        return

    timestamp = datetime.now().strftime("%Y%m%d-%H%M%S-%f")
    backup_dir = (
        ROOT / "build" / "overall-repair-work"
        / ("phase12-integrated-" + timestamp)
    )

    if backup_dir.exists():
        stop("备份目录已存在，拒绝覆盖")

    temp_file = SOURCE.with_name(
        SOURCE.name + ".phase12-" + timestamp + ".tmp"
    )

    if temp_file.exists():
        stop("临时文件已存在，拒绝覆盖")

    backup_dir.mkdir(parents=True)
    backup_file = backup_dir / "ide_pio.c.before"

    shutil.copy2(SOURCE, backup_file)

    try:
        temp_file.write_text(updated, encoding="utf-8")
        os.replace(temp_file, SOURCE)

        written = SOURCE.read_text(encoding="utf-8")
        require(written == updated, "写入后内容校验失败")
        validate(written)

    except Exception:
        if temp_file.exists():
            temp_file.unlink()
        shutil.copy2(backup_file, SOURCE)
        raise

    print("[PASS] IDE IDENTIFY 完成等待已检查")
    print("[PASS] IDE 重复注册处理已修复")
    print("[PASS] IDE 容量变化回滚路径已加入")
    print("[PASS] 初始化不再无条件清空旧设备状态")
    print("[PASS] READ / WRITE / FLUSH 结构验证通过")
    print("[BACKUP]", backup_file.relative_to(ROOT))
    print("[DONE] 第一批 IDE PIO 修复已写入")


if __name__ == "__main__":
    main()
