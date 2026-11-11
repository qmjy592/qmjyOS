from pathlib import Path
from datetime import datetime
import shutil
import sys

builder = Path("tools/qboot/build_qfs_ide_images.sh")
tester = Path("tools/qboot/test_qfs_ide.sh")

if not builder.is_file() or not tester.is_file():
    sys.exit("[STOP] QFS/IDE 构建或测试脚本不存在")

b = builder.read_text()
t = tester.read_text()

builder_old = '''BUILD_ROOT="build/qfs-ide"
FORMAT_DIR="build/qboot-qfs-format"
VERIFY_DIR="build/qboot-qfs-verify"'''

builder_new = '''BUILD_ROOT="${QFS_IDE_BUILD_ROOT:-build/qfs-ide}"
FORMAT_DIR="${QFS_FORMAT_DIR:-build/qboot-qfs-format}"
VERIFY_DIR="${QFS_VERIFY_DIR:-build/qboot-qfs-verify}"'''

test_old = '''FORMAT_IMAGE="${QFS_FORMAT_IMAGE:-build/qboot-qfs-format/qboot-bios.img}"
VERIFY_IMAGE="${QFS_VERIFY_IMAGE:-build/qboot-qfs-verify/qboot-bios.img}"
TIMEOUT_SECONDS=20'''

test_new = '''RUN_ID="${QFS_IDE_RUN_ID:-$(date +%Y%m%d-%H%M%S)}"
RUN_ROOT="build/phase12-unified/qfs-ide-${RUN_ID}"

if [ -e "$RUN_ROOT" ]; then
    echo "[QFS IDE TEST FAIL] Run directory already exists: $RUN_ROOT" >&2
    exit 1
fi

QFS_IDE_BUILD_ROOT="$RUN_ROOT/build"
QFS_FORMAT_DIR="$RUN_ROOT/format"
QFS_VERIFY_DIR="$RUN_ROOT/verify"

export QFS_IDE_BUILD_ROOT QFS_FORMAT_DIR QFS_VERIFY_DIR

FORMAT_IMAGE="${QFS_FORMAT_IMAGE:-$QFS_FORMAT_DIR/qboot-bios.img}"
VERIFY_IMAGE="${QFS_VERIFY_IMAGE:-$QFS_VERIFY_DIR/qboot-bios.img}"
TIMEOUT_SECONDS=20'''

test_dir_old = '''TEST_DIR="$(mktemp -d "${TMPDIR:-/tmp}/qmjyos-qfs-ide.XXXXXX")"
TEST_DISK="$TEST_DIR/qfs-test-disk.img"'''

test_dir_new = '''TEST_DIR="$RUN_ROOT/runtime"
mkdir -p "$TEST_DIR"
TEST_DISK="$TEST_DIR/qfs-test-disk.img"'''

if b.count(builder_old) != 1:
    sys.exit("[STOP] 构建脚本目录配置不符合预期，未修改")
if t.count(test_old) != 1:
    sys.exit("[STOP] 测试脚本镜像配置不符合预期，未修改")
if t.count(test_dir_old) != 1:
    sys.exit("[STOP] 测试临时目录配置不符合预期，未修改")

stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
backup_dir = Path("build/phase12-unified/backups")
backup_dir.mkdir(parents=True, exist_ok=True)

backups = [
    backup_dir / f"build_qfs_ide_images.sh.{stamp}.before",
    backup_dir / f"test_qfs_ide.sh.{stamp}.before",
]

if any(p.exists() for p in backups):
    sys.exit("[STOP] 备份文件已存在，拒绝覆盖")

shutil.copy2(builder, backups[0])
shutil.copy2(tester, backups[1])

b = b.replace(builder_old, builder_new, 1)
t = t.replace(test_old, test_new, 1)
t = t.replace(test_dir_old, test_dir_new, 1)

builder.write_text(b)
tester.write_text(t)

print("[BACKUP]", backups[0])
print("[BACKUP]", backups[1])
print("[PASS] QFS/IDE 构建目录支持独立路径")
print("[PASS] 测试目录改为项目内部独立目录")
print("[PASS] 不再使用 mktemp 或 /tmp")
print("[KEEP] 原有 build/qfs-ide 及历史镜像均保留")
