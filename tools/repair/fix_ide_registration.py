from pathlib import Path
from datetime import datetime
import re
import shutil
import sys

target = Path("drivers/block/ide_pio.c")

if not target.is_file():
    sys.exit("[STOP] 找不到 IDE 源文件")

source = target.read_text()

pattern = re.compile(
    r"static\s+qm_status_t\s+ide_register_disk\s*"
    r"\(\s*qm_u8_t\s+slave\s*,\s*qm_u32_t\s+sectors\s*\)"
    r"\s*\{",
    re.S,
)

matches = list(pattern.finditer(source))

if len(matches) != 1:
    sys.exit("[STOP] ide_register_disk 定义数量不是 1，拒绝修改")

start = matches[0].start()
brace = source.find("{", matches[0].start())
depth = 0
end = None

for i in range(brace, len(source)):
    if source[i] == "{":
        depth += 1
    elif source[i] == "}":
        depth -= 1
        if depth == 0:
            end = i + 1
            break

if end is None:
    sys.exit("[STOP] 函数括号不匹配")

old = source[start:end]

required = [
    "qm_block_device_info_t",
    "qm_block_device_id_t",
    "qm_block_get_info",
    "qm_block_device_unregister",
    "qm_block_device_register",
    "ide_block_ops",
]

if not all(x in source for x in required):
    sys.exit("[STOP] 缺少必要接口，拒绝修改")

new = r'''static qm_status_t ide_register_disk(
    qm_u8_t slave,
    qm_u32_t sectors)
{
    qm_block_device_info_t info;
    qm_block_device_info_t existing;
    qm_block_device_id_t id;
    qm_status_t status;

    if (slave != IDE_DEVICE_MASTER &&
        slave != IDE_DEVICE_SLAVE)
        return (qm_status_t)-1;

    if (sectors == 0 || sectors > 0x10000000U)
        return (qm_status_t)-1;

    id = slave == IDE_DEVICE_MASTER
       ? QM_IDE_PRIMARY_MASTER
       : QM_IDE_PRIMARY_SLAVE;

    info.id = id;
    info.block_count = sectors;
    info.block_size = IDE_SECTOR_SIZE;
    info.flags = 0;
    info.name = slave == IDE_DEVICE_MASTER
              ? "ide-primary-master"
              : "ide-primary-slave";

    status = qm_block_get_info(id, &existing);

    if (status == 0) {
        if (existing.id == info.id &&
            existing.block_count == info.block_count &&
            existing.block_size == info.block_size &&
            existing.flags == info.flags)
            return (qm_status_t)0;

        status = qm_block_device_unregister(id);
        if (status != 0)
            return status;
    } else if (status != (qm_status_t)-1) {
        return status;
    }

    return qm_block_device_register(&info, &ide_block_ops);
}'''

stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
backup = Path("build/phase12-repair") / f"ide_pio.c.{stamp}.before"
backup.parent.mkdir(parents=True, exist_ok=True)

if backup.exists():
    sys.exit("[STOP] 备份文件已存在")

shutil.copy2(target, backup)

updated = source[:start] + new + source[end:]
target.write_text(updated)

print("[OK] IDE 注册函数已替换")
print(f"[BACKUP] {backup}")
print("[INFO] 其他函数未主动修改")
