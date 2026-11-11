from pathlib import Path
from datetime import datetime
import re
import shutil
import sys

IDE = Path("drivers/block/ide_pio.c")
MBR = Path("fs/partition/mbr.c")

def get_function(src, name):
    m = re.search(
        r'\b' + re.escape(name) + r'\s*\([^;{}]*\)\s*\{',
        src
    )
    if not m:
        raise RuntimeError(f"找不到函数: {name}")
    start = m.end()
    depth = 1
    i = start
    while i < len(src) and depth:
        if src[i] == "{":
            depth += 1
        elif src[i] == "}":
            depth -= 1
        i += 1
    if depth:
        raise RuntimeError(f"函数括号不匹配: {name}")
    return m.start(), i, src[m.start():i]

def replace_function(src, name, new_func):
    start, end, _ = get_function(src, name)
    return src[:start] + new_func + src[end:]

def reorder_device_selection(src, name):
    _, _, func = get_function(src, name)
    pattern = re.compile(
        r'(?P<wait>if\s*\(\s*ide_wait\s*\(\s*0\s*\)\s*!=\s*0\s*\)'
        r'\s*(?:\{[^{}]*\}|return\s+[^;]+;))'
        r'(?P<gap>\s*)'
        r'(?P<select>ide_select\s*\(\s*slave\s*\)\s*;)',
        re.S
    )
    updated, count = pattern.subn(
        lambda m: m.group("select") + m.group("gap") + m.group("wait"),
        func,
        count=1
    )
    if count != 1:
        raise RuntimeError(
            f"{name}: 未找到唯一且安全的“先等待后选择设备”代码结构"
        )
    return replace_function(src, name, updated)

def add_mbr_overlap_check(src):
    start, end, func = get_function(src, "qm_mbr_scan")

    if re.search(r'out_partitions\s*\[\s*j\s*\]\.start_lba', func):
        raise RuntimeError("MBR 重叠检查疑似已经存在，拒绝重复修改")

    declaration = re.search(r'\bqm_u64_t\s+end\s*;', func)
    if not declaration:
        raise RuntimeError("找不到 MBR 分区结束位置变量声明")

    bounds = re.search(
        r'if\s*\(\s*end\s*<\s*start\s*\|\|\s*'
        r'end\s*>\s*device_info\.block_count\s*\)\s*'
        r'\{\s*return\s+([^;]+);\s*\}',
        func,
        re.S
    )
    if not bounds:
        raise RuntimeError("找不到 MBR 原有分区边界检查，拒绝修改")

    failure = bounds.group(1).strip()
    func = func[:declaration.end()] + "\n        qm_u32_t j;" + func[declaration.end():]

    bounds = re.search(
        r'if\s*\(\s*end\s*<\s*start\s*\|\|\s*'
        r'end\s*>\s*device_info\.block_count\s*\)\s*'
        r'\{\s*return\s+[^;]+;\s*\}',
        func,
        re.S
    )
    insert = """
        for (j = 0; j < found; ++j) {
            qm_u64_t previous_start =
                (qm_u64_t)out_partitions[j].start_lba;
            qm_u64_t previous_end =
                previous_start +
                (qm_u64_t)out_partitions[j].sector_count;

            if ((qm_u64_t)start < previous_end &&
                previous_start < end) {
                return %s;
            }
        }
""" % failure

    pos = bounds.end()
    func = func[:pos] + insert + func[pos:]
    return src[:start] + func + src[end:]

def main():
    for p in (IDE, MBR):
        if not p.is_file():
            raise RuntimeError(f"缺少文件: {p}")

    ide_original = IDE.read_text()
    mbr_original = MBR.read_text()

    # 先在内存中完成全部匹配；任何一项失败都不写源码。
    ide_updated = reorder_device_selection(
        ide_original, "ide_transfer_one"
    )
    ide_updated = reorder_device_selection(
        ide_updated, "ide_flush"
    )
    mbr_updated = add_mbr_overlap_check(mbr_original)

    if ide_updated == ide_original or mbr_updated == mbr_original:
        raise RuntimeError("修改结果为空或不完整，停止写入")

    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    backup = Path("build/overall-repair-work") / f"phase2-backup-{stamp}"
    backup.mkdir(parents=True, exist_ok=False)

    shutil.copy2(IDE, backup / "ide_pio.c")
    shutil.copy2(MBR, backup / "mbr.c")

    IDE.write_text(ide_updated)
    MBR.write_text(mbr_updated)

    # 写入后立即验证关键结构。
    _, _, transfer = get_function(IDE.read_text(), "ide_transfer_one")
    _, _, flush = get_function(IDE.read_text(), "ide_flush")
    _, _, scan = get_function(MBR.read_text(), "qm_mbr_scan")

    for name, body in (("ide_transfer_one", transfer), ("ide_flush", flush)):
        a = body.find("ide_select(slave)")
        b = body.find("ide_wait(0)")
        if a < 0 or b < 0 or a > b:
            raise RuntimeError(f"{name}: 写入后顺序验证失败")

    if "out_partitions[j].start_lba" not in scan:
        raise RuntimeError("MBR 重叠检测写入后验证失败")

    print("[PASS] IDE 传输设备选择顺序已修复")
    print("[PASS] IDE Flush 设备选择顺序已修复")
    print("[PASS] MBR 主分区重叠检测已添加")
    print(f"[BACKUP] {backup}")
    print("[NOTE] 尚未进行编译或 QEMU 实测")

if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"[STOP] {exc}")
        sys.exit(1)
