from pathlib import Path
from datetime import datetime
import re
import shutil
import sys

target = Path("drivers/block/ide_pio.c")
if not target.is_file():
    sys.exit("[STOP] 找不到 drivers/block/ide_pio.c")

source = target.read_text()

helper = re.search(
    r"static\s+int\s+ide_name_equal\s*\(\s*"
    r"const\s+char\s*\*\s*a\s*,\s*"
    r"const\s+char\s*\*\s*b\s*\)",
    source
)
if not helper:
    sys.exit("[STOP] 名称比较函数不存在或签名不匹配")

old = "existing.flags == info.flags)"
new = (
    "existing.flags == info.flags &&\n"
    "            ide_name_equal(existing.name, info.name))"
)

if source.count(old) != 1:
    if "ide_name_equal(existing.name, info.name)" in source:
        print("[PASS] 名称比较已经接入，无需重复修改")
        sys.exit(0)
    sys.exit("[STOP] 注册判断结构与预期不一致，未修改文件")

backup_dir = Path("build/phase12-repair")
backup_dir.mkdir(parents=True, exist_ok=True)
backup = backup_dir / (
    "ide_pio.c.before-name-fix-" +
    datetime.now().strftime("%Y%m%d-%H%M%S")
)

if backup.exists():
    sys.exit(f"[STOP] 备份已存在：{backup}")

shutil.copy2(target, backup)
target.write_text(source.replace(old, new, 1))

print(f"[BACKUP] {backup}")
print("[PASS] IDE 设备注册加入名称一致性判断")
