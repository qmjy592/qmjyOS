#!/usr/bin/env python3
"""QBuild v0.5.0 module planner."""

from pathlib import Path
import argparse
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
CONFIG = ROOT / "config/modules.conf"
DEPS = ROOT / "config/modules.dep"
STATUS = ROOT / "config/modules.status"
OUTPUT = ROOT / "build/modules.mk"

SOURCES_32 = {
    "QBOOT_BIOS": [
        ("boot/bios/boot.S", "boot/bios/boot.o"),
    ],
}

SOURCES_64 = {
    "ARCH_X86_64": [
        ("arch/x86_64/boot/entry64.S", "arch/x86_64/boot/entry64.o"),
        ("arch/x86_64/gdt/gdt_load.S", "arch/x86_64/gdt/gdt_load.o"),
        ("arch/x86_64/cpu/ring3_entry.S", "arch/x86_64/cpu/ring3_entry.o"),
        ("arch/x86_64/cpu/switch.S", "arch/x86_64/cpu/switch.o"),
        ("arch/x86_64/idt/idt.c", "arch/x86_64/idt/idt.o"),
        ("arch/x86_64/interrupts/exception.c", "arch/x86_64/interrupts/exception.o"),
        ("arch/x86_64/interrupts/excep.S", "arch/x86_64/interrupts/excep.o"),
        ("arch/x86_64/interrupts/pic.c", "arch/x86_64/interrupts/pic.o"),
        ("arch/x86_64/interrupts/timer.c", "arch/x86_64/interrupts/timer.o"),
        ("arch/x86_64/interrupts/irq.c", "arch/x86_64/interrupts/irq.o"),
        ("arch/x86_64/interrupts/irq_entry.S", "arch/x86_64/interrupts/irq_entry.o"),
    ],
    "KERNEL_CORE": [
        ("kernel/core/main.c", "kernel/core/main.o"),
    ],
    "DRIVER_SERIAL": [
        ("drivers/serial/serial.c", "drivers/serial/serial.o"),
    ],
    "MM_PHYSICAL": [
        ("mm/physical/memory.c", "mm/physical/memory.o"),
    ],
    "MM_PAGING": [
        ("mm/paging/paging.c", "mm/paging/paging.o"),
    ],
    "SCHEDULER": [
        ("sched/core/task.c", "sched/core/task.o"),
    ],
    "FS_VFS": [
        ("fs/vfs/vfs.c", "fs/vfs/vfs.o"),
    ],
    "FS_RAMFS": [
        ("fs/ramfs/ramfs.c", "fs/ramfs/ramfs.o"),
        ("fs/core/fs_init.c", "fs/core/fs_init.o"),
    ],
}


def fail(message):
    raise ValueError(message)


def read_pairs(path, allowed_values):
    result = {}
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            fail(f"{path}:{number}: 缺少 '='")
        name, value = (part.strip() for part in line.split("=", 1))
        if not re.fullmatch(r"[A-Z][A-Z0-9_]*", name):
            fail(f"{path}:{number}: 非法模块名称 {name}")
        if name in result:
            fail(f"{path}:{number}: 重复定义 {name}")
        if value not in allowed_values:
            fail(f"{path}:{number}: {name} 的值无效：{value}")
        result[name] = value
    return result


def read_config():
    result = read_pairs(
        CONFIG, {"enabled", "disabled", "optional"}
    )
    if not result:
        fail("模块配置为空")
    return result


def read_status():
    result = read_pairs(
        STATUS, {"implemented", "experimental", "planned"}
    )
    if not result:
        fail("模块状态清单为空")
    return result


def read_dependencies():
    result = {}
    for number, raw in enumerate(DEPS.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if ":" not in line:
            fail(f"{DEPS}:{number}: 缺少 ':'")
        name, values = (part.strip() for part in line.split(":", 1))
        if not re.fullmatch(r"[A-Z][A-Z0-9_]*", name):
            fail(f"{DEPS}:{number}: 非法模块名 {name}")
        if name in result:
            fail(f"{DEPS}:{number}: 重复定义 {name}")
        dependencies = values.split() if values else []
        if len(dependencies) != len(set(dependencies)):
            fail(f"{DEPS}:{number}: {name} 存在重复依赖")
        result[name] = dependencies
    return result


def source_map():
    result = {}
    for module, entries in SOURCES_32.items():
        result[module] = result.get(module, []) + [
            (source, obj, 32) for source, obj in entries
        ]
    for module, entries in SOURCES_64.items():
        result[module] = result.get(module, []) + [
            (source, obj, 64) for source, obj in entries
        ]
    return result


def validate(config, deps, status):
    names = set(config)

    if set(deps) != names:
        fail(
            "配置与依赖表模块不一致："
            f"缺少定义={sorted(names - set(deps))}，"
            f"多余定义={sorted(set(deps) - names)}"
        )

    if set(status) != names:
        fail(
            "配置与状态清单模块不一致："
            f"缺少状态={sorted(names - set(status))}，"
            f"多余状态={sorted(set(status) - names)}"
        )

    for module, requirements in deps.items():
        for requirement in requirements:
            if requirement not in names:
                fail(f"{module} 依赖未知模块 {requirement}")

    # Detect cycles in the entire dependency graph.
    visiting = []
    visited = set()

    def visit(module):
        if module in visiting:
            start = visiting.index(module)
            fail("循环依赖：" + " -> ".join(visiting[start:] + [module]))
        if module in visited:
            return
        visiting.append(module)
        for requirement in deps[module]:
            visit(requirement)
        visiting.pop()
        visited.add(module)

    for module in sorted(names):
        visit(module)

    # Every enabled module must have enabled direct dependencies.
    for module, state in config.items():
        if state != "enabled":
            continue
        for requirement in deps[module]:
            if config[requirement] != "enabled":
                fail(
                    f"{module} 已启用，但依赖 {requirement} "
                    f"的配置状态为 {config[requirement]}"
                )

    mapped = source_map()
    owners = {}

    for module, entries in mapped.items():
        for source, obj, bits in entries:
            source_path = ROOT / source
            if not source_path.is_file():
                fail(f"{module} 的源码不存在：{source}")
            if obj in owners:
                fail(f"对象文件重复映射：{obj} ({owners[obj]} 和 {module})")
            owners[obj] = module

    for module, state in status.items():
        if state == "implemented" and module not in mapped and module not in {"QBUILD"}:
            fail(f"{module} 标记为 implemented，但没有源码映射")
        if state == "experimental" and module not in mapped and module not in {"QBUILD"}:
            fail(f"{module} 标记为 experimental，但没有源码映射")

    return mapped


def make_plan(config, deps, selected_names, mapped):
    if selected_names:
        unknown = sorted(set(selected_names) - set(config))
        if unknown:
            fail("未知模块：" + ", ".join(unknown))
        roots = set(selected_names)
    else:
        roots = {
            module for module, state in config.items()
            if state == "enabled"
        }

    selected = set()

    def include(module):
        if module in selected:
            return
        state = config[module]
        if state == "disabled":
            fail(f"模块 {module} 已禁用，不能参与构建")
        if not selected_names and state != "enabled":
            fail(f"启用模块依赖 {module}，但它不是 enabled")
        selected.add(module)
        for requirement in deps[module]:
            include(requirement)

    for module in sorted(roots):
        include(module)

    visiting = set()
    visited = set()
    order = []

    def visit(module):
        if module in visiting:
            fail(f"循环依赖：{module}")
        if module in visited:
            return
        visiting.add(module)
        for requirement in sorted(deps[module]):
            if requirement in selected:
                visit(requirement)
        visiting.remove(module)
        visited.add(module)
        order.append(module)

    for module in sorted(selected):
        visit(module)

    objects32 = []
    objects64 = []

    for module in order:
        for source, obj, bits in mapped.get(module, []):
            if bits == 32:
                objects32.append(obj)
            else:
                objects64.append(obj)

    return order, objects32, objects64


def write_makefile(order, objects32, objects64):
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "# Generated by QBuild v0.5.0. Do not edit.",
        "QBUILD_MODULE_PLAN := " + " ".join(order),
        "OBJS_32 := " + " ".join(objects32),
        "OBJS_64 := " + " ".join(objects64),
        "",
    ]
    OUTPUT.write_text("\n".join(lines), encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description="QBuild v0.5.0 module planner")
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--show", action="store_true")
    parser.add_argument("--modules", action="store_true")
    parser.add_argument("--deps", action="store_true")
    parser.add_argument("--make", action="store_true")
    parser.add_argument("--objects", action="store_true")
    parser.add_argument("--module", action="append", default=[])
    args = parser.parse_args()

    try:
        config = read_config()
        deps = read_dependencies()
        status = read_status()
        mapped = validate(config, deps, status)
        order, objects32, objects64 = make_plan(
            config, deps, args.module, mapped
        )

        if args.check:
            print("模块配置与依赖完整性：PASS")
            print("循环依赖检测：PASS")
            print("模块状态清单：PASS")
            print("源码映射存在性：PASS")
            print("对象文件映射唯一性：PASS")
            print("启用模块依赖检查：PASS")
            print(f"模块总数：{len(config)}")
            print(f"源码已映射模块：{len(mapped)}")
            print(f"当前计划模块：{len(order)}")
            print(
                "状态统计："
                f"implemented={sum(x == 'implemented' for x in status.values())}, "
                f"experimental={sum(x == 'experimental' for x in status.values())}, "
                f"planned={sum(x == 'planned' for x in status.values())}"
            )

        if args.modules:
            print(f"{'MODULE':<28} {'CONFIG':<10} {'STATUS':<14} SOURCE")
            print("-" * 75)
            for module in sorted(config):
                source_state = "mapped" if module in mapped else "no-source"
                print(
                    f"{module:<28} {config[module]:<10} "
                    f"{status[module]:<14} {source_state}"
                )

        if args.deps:
            for module in sorted(deps):
                print(f"{module}: {' '.join(deps[module])}")

        if args.make:
            write_makefile(order, objects32, objects64)
            print(f"模块 Makefile 已生成：{OUTPUT.relative_to(ROOT)}")

        if args.objects:
            objects = objects32 + objects64
            if not objects:
                fail("所选模块没有可构建的对象文件")
            for obj in objects:
                print(obj)

        if args.show or not any((
            args.check, args.modules, args.deps,
            args.make, args.objects
        )):
            print("QBuild v0.5.0 模块构建计划")
            print("==========================")
            print("拓扑排序：")
            for index, module in enumerate(order, 1):
                print(f"  {index:02d}. {module}")
            print()
            print(f"参与计划模块：{len(order)}")
            print(f"32 位对象文件：{len(objects32)}")
            print(f"64 位对象文件：{len(objects64)}")
            print()
            print("模块状态与源码映射相互独立。")
            print("规划中的模块不会被伪装成已实现模块。")

        return 0

    except (OSError, ValueError) as error:
        print(f"QBuild 模块计划错误：{error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
