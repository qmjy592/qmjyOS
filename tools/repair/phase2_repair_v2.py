from pathlib import Path
from datetime import datetime
import re
import shutil
import sys

IDE = Path("drivers/block/ide_pio.c")
MBR = Path("fs/partition/mbr.c")

def get_func(src, name):
    m = re.search(r'\b' + re.escape(name) + r'\s*\([^;{}]*\)\s*\{', src)
    if not m:
        raise RuntimeError("找不到函数: " + name)
    depth = 1
    i = m.end()
    while i < len(src) and depth:
        if src[i] == "{":
            depth += 1
        elif src[i] == "}":
            depth -= 1
        i += 1
    if depth:
        raise RuntimeError("函数括号不匹配: " + name)
    return m.start(), i, src[m.start():i]

def replace_func(src, name, new):
    a, b, _ = get_func(src, name)
    return src[:a] + new + src[b:]

def reorder_ide(src, name):
    _, _, body = get_func(src, name)
    pat = re.compile(
        r'(?P<wait>if\s*\(\s*ide_wait\s*\(\s*0\s*\)\s*!=\s*0\s*\)'
        r'\s*(?:\{[^{}]*\}|return\s+[^;]+;))'
        r'(?P<gap>\s*)'
        r'(?P<select>ide_select\s*\(\s*slave\s*\)\s*;)',
        re.S
    )
    new, n = pat.subn(
        lambda m: m.group("select") + m.group("gap") + m.group("wait"),
        body, count=1
    )
    if n != 1:
        raise RuntimeError(name + ": IDE 选择顺序匹配失败")
    return replace_func(src, name, new)

def repair_mbr(src):
    a, b, body = get_func(src, "qm_mbr_scan")

    if "out_partitions[j].start_lba" in body:
        raise RuntimeError("MBR 重叠检查已存在，拒绝重复修改")

    decl = re.search(r'qm_u64_t\s+end\s*;', body)
    if not decl:
        raise RuntimeError("找不到 end 变量声明")

    body = body[:decl.end()] + "\n        qm_u32_t j;" + body[decl.end():]

    boundary = re.search(
        r'if\s*\(\s*end\s*<\s*start\s*\|\|\s*'
        r'end\s*>\s*device_info\.block_count\s*\)\s*'
        r'return\s+-3\s*;',
        body
    )
    if not boundary:
        raise RuntimeError("实际 MBR 边界检查匹配失败")

    check = """
        /* Reject overlapping primary partition ranges. */
        for (j = 0; j < found; ++j) {
            qm_u64_t previous_start =
                (qm_u64_t)out_partitions[j].start_lba;
            qm_u64_t previous_end =
                previous_start +
                (qm_u64_t)out_partitions[j].sector_count;

            if ((qm_u64_t)start < previous_end &&
                previous_start < end)
                return -3;
        }
"""
    pos = boundary.end()
    body = body[:pos] + "\n" + check + body[pos:]
    return src[:a] + body + src[b:]

def main():
    if not IDE.is_file() or not MBR.is_file():
        raise RuntimeError("IDE 或 MBR 源文件缺失")

    old_ide = IDE.read_text()
    old_mbr = MBR.read_text()

    # 所有修改先在内存中完成。
    new_ide = reorder_ide(old_ide, "ide_transfer_one")
    new_ide = reorder_ide(new_ide, "ide_flush")
    new_mbr = repair_mbr(old_mbr)

    # 写入前检查结果。
    for name in ("ide_transfer_one", "ide_flush"):
        _, _, body = get_func(new_ide, name)
        if body.find("ide_select(slave)") > body.find("ide_wait(0)"):
            raise RuntimeError(name + ": 顺序预检失败")

    _, _, mbr_body = get_func(new_mbr, "qm_mbr_scan")
    if "out_partitions[j].start_lba" not in mbr_body:
        raise RuntimeError("MBR 重叠检查预检失败")

    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    backup = Path("build/overall-repair-work") / ("phase2-v2-" + stamp)
    backup.mkdir(parents=True, exist_ok=False)
    shutil.copy2(IDE, backup / "ide_pio.c")
    shutil.copy2(MBR, backup / "mbr.c")

    try:
        IDE.write_text(new_ide)
        MBR.write_text(new_mbr)
    except Exception:
        IDE.write_text(old_ide)
        MBR.write_text(old_mbr)
        raise

    print("[PASS] IDE 传输设备选择顺序")
    print("[PASS] IDE Flush 设备选择顺序")
    print("[PASS] MBR 主分区重叠检测")
    print("[BACKUP]", backup)

if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        print("[STOP]", e)
        sys.exit(1)
