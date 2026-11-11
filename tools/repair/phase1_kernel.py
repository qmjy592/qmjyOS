#!/usr/bin/env python3
from pathlib import Path
import re
import sys

ROOT = Path.cwd()
REPORT = ROOT / "build/overall-repair-work/phase1-kernel-audit.txt"

FILES = [
    "Makefile",
    "build/modules.mk",
    "mm/paging/paging.c",
    "mm/paging/paging.h",
    "mm/physical/memory.c",
    "mm/physical/memory.h",
    "sched/core/task.c",
    "sched/core/task.h",
    "arch/x86_64/gdt/gdt_load.S",
    "arch/x86_64/idt/idt.c",
    "arch/x86_64/interrupts/irq.c",
    "arch/x86_64/interrupts/irq.S",
    "arch/x86_64/interrupts/irq_entry.S",
    "arch/x86_64/interrupts/excep.S",
    "arch/x86_64/interrupts/exception.c",
]

results = []
errors = []

def record(level, message):
    results.append(f"[{level}] {message}")
    print(f"[{level}] {message}")

def read_file(name):
    path = ROOT / name
    if not path.is_file():
        record("MISSING", name)
        return None
    try:
        return path.read_text()
    except Exception as exc:
        errors.append(name)
        record("ERROR", f"{name}: {exc}")
        return None

record("INFO", "qmjyOS Phase 1 kernel audit")
record("INFO", "只读检查；本脚本不修改任何源码")

sources = {}
for name in FILES:
    sources[name] = read_file(name)

# 1. Paging
paging = sources.get("mm/paging/paging.c") or ""

for symbol in (
    "split_large_page",
    "rollback_created",
    "vm_map_page",
    "vm_unmap_page",
    "vm_query_page",
    "vm_activate",
    "vm_invalidate_page",
    "vm_nx_enabled",
    "vm_self_test",
):
    if re.search(r"\b" + symbol + r"\s*\(", paging):
        record("PASS", f"分页接口存在：{symbol}")
    else:
        record("WARN", f"分页接口缺失或名称变化：{symbol}")

if "VM_TABLE_COUNT" in paging and "VM_LARGE_SIZE" in paging:
    record("PASS", "检测到静态页表池和 2 MiB 大页拆分实现")
else:
    record("WARN", "分页实现与已审查版本不同")

# 2. Physical memory
memory = sources.get("mm/physical/memory.c") or ""

for token in (
    "mm_parse_multiboot_mmap",
    "mm_reserve",
    "allocator_disable",
    "firmware_reserved",
    "UINT64_MAX",
):
    if token in memory:
        record("PASS", f"物理内存检查点存在：{token}")
    else:
        record("WARN", f"物理内存检查点缺失：{token}")

# 3. Scheduler
task = sources.get("sched/core/task.c") or ""

for token in ("task_init", "task_yield", "task_start", "task_exit"):
    if re.search(r"\b" + token + r"\s*\(", task):
        record("PASS", f"调度器入口存在：{token}")
    else:
        record("WARN", f"调度器入口缺失：{token}")

# 4. GDT
gdt = sources.get("arch/x86_64/gdt/gdt_load.S") or ""

if "lgdt" in gdt and "lretq" in gdt:
    record("PASS", "GDT 加载及 CS 远返回刷新指令存在")
else:
    record("WARN", "GDT 加载或 CS 刷新实现需要复核")

# 5. Interrupt entry selection
modules = sources.get("build/modules.mk") or ""
makefile = sources.get("Makefile") or ""

irq_entry = sources.get("arch/x86_64/interrupts/irq_entry.S") or ""
irq_legacy = sources.get("arch/x86_64/interrupts/irq.S") or ""

if "irq_entry.S" in modules or "irq_entry.o" in modules:
    record("PASS", "模块配置引用 irq_entry.S")
else:
    record("WARN", "模块配置未明确引用 irq_entry.S")

if "irq.S" in modules or "irq.o" in modules:
    record("WARN", "模块配置同时或单独引用旧 irq.S，需要检查重复符号")
else:
    record("INFO", "模块配置未发现旧 irq.S 的直接引用")

for name, source in (
    ("irq_entry.S", irq_entry),
    ("irq.S", irq_legacy),
):
    if not source:
        continue
    if "andq $-16, %rsp" in source:
        record("PASS", f"{name} 包含栈对齐")
    else:
        record("WARN", f"{name} 未发现明确的 16 字节栈对齐")

    if "cld" in source:
        record("PASS", f"{name} 在调用 C 处理程序前清除 DF")
    else:
        record("WARN", f"{name} 未发现 cld")

# 6. IDT and exceptions
idt = sources.get("arch/x86_64/idt/idt.c") or ""
exception = sources.get("arch/x86_64/interrupts/exception.c") or ""
excep_asm = sources.get("arch/x86_64/interrupts/excep.S") or ""

if "lidt" in idt:
    record("PASS", "IDT 加载指令存在")
else:
    record("WARN", "未发现 lidt")

if "exception_handler" in exception and "CR2" in exception:
    record("PASS", "异常处理及页故障地址诊断存在")
else:
    record("WARN", "异常诊断功能需要复核")

if "isr_common" in excep_asm:
    record("PASS", "异常汇编公共入口存在")
else:
    record("WARN", "异常公共入口需要复核")

# 7. Build references
if "paging" in modules:
    record("PASS", "模块配置包含分页相关引用")
else:
    record("WARN", "模块配置未发现 paging 关键字")

if "memory" in modules:
    record("PASS", "模块配置包含物理内存相关引用")
else:
    record("WARN", "模块配置未发现 memory 关键字")

# 8. Save report
REPORT.parent.mkdir(parents=True, exist_ok=True)

content = "\n".join(results)
REPORT.write_text(content + "\n")

print()
print("========== PHASE 1 AUDIT SUMMARY ==========")
print(f"检查文件数：{len(FILES)}")
print(f"异常读取数：{len(errors)}")
print(f"报告位置：{REPORT.relative_to(ROOT)}")
print("源码修改：0")
print("===========================================")

if errors:
    sys.exit(2)
