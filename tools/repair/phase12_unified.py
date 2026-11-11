#!/usr/bin/env python3
from pathlib import Path
import subprocess
import sys
from datetime import datetime

ROOT = Path.cwd()
LOG_DIR = ROOT / "build/phase12-unified"
LOG_DIR.mkdir(parents=True, exist_ok=True)

log = LOG_DIR / (
    "phase12-" + datetime.now().strftime("%Y%m%d-%H%M%S") + ".log"
)

def run(title, command):
    print("\n" + "=" * 64)
    print(title)
    print("=" * 64)

    result = subprocess.run(
        command,
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT
    )

    print(result.stdout)
    with log.open("a") as f:
        f.write("\n" + "=" * 64 + "\n")
        f.write(title + "\n")
        f.write(result.stdout)

    if result.returncode != 0:
        print("[STOP] " + title)
        sys.exit(result.returncode)

    print("[PASS] " + title)

print("qmjyOS 第一批 22 合一统一执行")
print("工作目录:", ROOT)
print("日志:", log)

# 1. 阶段一：内核静态审查
phase1 = ROOT / "tools/repair/phase1_kernel.py"
if phase1.exists():
    run("阶段一：内核模块审查", [sys.executable, str(phase1)])
else:
    print("[WARN] 未找到 phase1_kernel.py，跳过该项")

# 2. IDE 修复脚本语法
for script in (
    "tools/repair/phase2_ide_repair.py",
    "tools/repair/fix_ide_registration.py",
    "tools/repair/fix_ide_name_warning.py",
):
    path = ROOT / script
    if path.exists():
        run("脚本语法检查：" + script,
            [sys.executable, "-m", "py_compile", str(path)])

# 3. 核心源码和构建配置存在性
required = [
    "mm/physical/memory.c",
    "mm/paging/paging.c",
    "mm/paging/paging.h",
    "arch/x86_64/gdt/gdt_load.S",
    "arch/x86_64/idt/idt.c",
    "arch/x86_64/interrupts/irq.c",
    "arch/x86_64/interrupts/irq_entry.S",
    "arch/x86_64/interrupts/exception.c",
    "sched/core/task.c",
    "drivers/block/block.c",
    "drivers/block/ide_pio.c",
    "fs/partition/mbr.c",
    "Makefile",
]

missing = [p for p in required if not (ROOT / p).is_file()]
if missing:
    print("[STOP] 缺少必要源码：")
    for item in missing:
        print("  -", item)
    sys.exit(2)

print("\n[PASS] 阶段一、阶段二核心源码文件存在")

# 4. 统一构建
run("第一批统一完整构建", ["make", "-j2"])

# 5. Git 差异格式检查
run("Git 差异格式检查", ["git", "diff", "--check"])

# 6. 构建产物检查
artifacts = ["kernel.elf", "kernel64.elf", "kernel64.bin"]
for item in artifacts:
    path = ROOT / item
    if not path.is_file() or path.stat().st_size == 0:
        print("[STOP] 构建产物缺失或为空：", item)
        sys.exit(3)
    print("[PASS]", item, path.stat().st_size, "bytes")

print("\n" + "=" * 64)
print("第一批统一执行入口完成")
print("注意：这不代表 QEMU 和真实磁盘读写测试已通过。")
print("完整日志：", log)
print("=" * 64)
