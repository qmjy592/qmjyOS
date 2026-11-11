from pathlib import Path
from datetime import datetime
import shutil
import sys

script = Path("tools/qboot/test_qfs_ide.sh")
if not script.is_file():
    sys.exit("[STOP] 测试脚本不存在")

source = script.read_text()

old = "build/qfs-ide"
if old not in source:
    sys.exit("[STOP] 未找到测试工作目录配置，拒绝猜测修改")

stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
new = f"build/qfs-ide-run-{stamp}"

backup = Path(f"build/phase12-unified/test_qfs_ide.sh.before-{stamp}")
backup.parent.mkdir(parents=True, exist_ok=True)

if backup.exists():
    sys.exit(f"[STOP] 备份已存在：{backup}")

shutil.copy2(script, backup)

script.write_text(source.replace(old, new))

print("[BACKUP]", backup)
print("[PASS] 本轮测试工作目录改为", new)
print("[KEEP] 原 build/qfs-ide 完整保留")
