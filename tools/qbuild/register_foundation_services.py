#!/usr/bin/env python3
from pathlib import Path
from datetime import datetime
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]

FILES = [
    ROOT / "config/modules.conf",
    ROOT / "config/modules.dep",
    ROOT / "config/modules.status",
    ROOT / "tools/qbuild/module_plan.py",
]

stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
backups = {}

def replace_once(content, old, new, label):
    count = content.count(old)
    if count != 1:
        raise RuntimeError(
            f"{label}：预期匹配 1 次，实际匹配 {count} 次"
        )
    return content.replace(old, new, 1)

try:
    original = {
        path: path.read_text(encoding="utf-8")
        for path in FILES
    }

    # --------------------------------------------------------
    # 1. 模块配置
    # --------------------------------------------------------
    conf = original[FILES[0]]
    conf = replace_once(
        conf,
        "# User space\n",
        """# User space
SERVICE_MANAGER=optional
SERVICE_REGISTRY=optional
SERVICE_LOGGING=optional
SERVICE_CONFIG=optional
INIT_MANAGER=optional
CAPABILITY=optional
""",
        "modules.conf 用户空间模块位置",
    )

    # --------------------------------------------------------
    # 2. 模块依赖
    # --------------------------------------------------------
    deps = original[FILES[1]]
    deps = replace_once(
        deps,
        "IPC: KERNEL_CORE MM_HEAP SCHEDULER",
        "IPC: RUNTIME SYSCALL",
        "IPC 依赖",
    )
    deps = replace_once(
        deps,
        "SYSTEM_SERVICES: RUNTIME SYSCALL",
        """SERVICE_MANAGER: RUNTIME
SERVICE_REGISTRY: RUNTIME
SERVICE_LOGGING: RUNTIME
SERVICE_CONFIG: RUNTIME
INIT_MANAGER: RUNTIME SERVICE_MANAGER SERVICE_REGISTRY SERVICE_LOGGING SERVICE_CONFIG
CAPABILITY: RUNTIME

SYSTEM_SERVICES: RUNTIME SYSCALL IPC SERVICE_MANAGER SERVICE_REGISTRY SERVICE_LOGGING SERVICE_CONFIG INIT_MANAGER CAPABILITY""",
        "SYSTEM_SERVICES 依赖",
    )

    # --------------------------------------------------------
    # 3. 模块状态
    # --------------------------------------------------------
    status = original[FILES[2]]
    status = replace_once(
        status,
        "IPC=planned",
        "IPC=experimental",
        "IPC 状态",
    )
    status = replace_once(
        status,
        "SYSTEM_SERVICES=planned",
        """SERVICE_MANAGER=experimental
SERVICE_REGISTRY=experimental
SERVICE_LOGGING=experimental
SERVICE_CONFIG=experimental
INIT_MANAGER=experimental
CAPABILITY=experimental
SYSTEM_SERVICES=planned""",
        "服务模块状态",
    )

    # --------------------------------------------------------
    # 4. QBuild 源码映射
    # --------------------------------------------------------
    planner = original[FILES[3]]

    user_map = '''

# User-space foundation services.
# These sources are tracked by QBuild but are NOT linked into
# the kernel's OBJS_32 or OBJS_64.
SOURCES_USER = {
    "IPC": [
        ("ipc/message/qm_ipc.c", "ipc/message/qm_ipc.user.o"),
    ],
    "SERVICE_MANAGER": [
        ("services/manager/qm_service.c", "services/manager/qm_service.user.o"),
    ],
    "SERVICE_REGISTRY": [
        ("services/registry/qm_registry.c", "services/registry/qm_registry.user.o"),
    ],
    "SERVICE_LOGGING": [
        ("services/logging/qm_log.c", "services/logging/qm_log.user.o"),
    ],
    "SERVICE_CONFIG": [
        ("services/config/qm_service_config.c", "services/config/qm_service_config.user.o"),
    ],
    "INIT_MANAGER": [
        ("init/startup/qm_init.c", "init/startup/qm_init.user.o"),
    ],
    "CAPABILITY": [
        ("security/capability/qm_cap.c", "security/capability/qm_cap.user.o"),
    ],
}
'''

    planner = replace_once(
        planner,
        "\n\n\ndef fail(message):",
        user_map + "\n\ndef fail(message):",
        "用户空间源码映射插入点",
    )

    old_source_map = '''def source_map():
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
'''

    new_source_map = '''def source_map():
    result = {}
    for module, entries in SOURCES_32.items():
        result[module] = result.get(module, []) + [
            (source, obj, 32) for source, obj in entries
        ]
    for module, entries in SOURCES_64.items():
        result[module] = result.get(module, []) + [
            (source, obj, 64) for source, obj in entries
        ]
    for module, entries in SOURCES_USER.items():
        result[module] = result.get(module, []) + [
            (source, obj, "user") for source, obj in entries
        ]
    return result
'''

    planner = replace_once(
        planner, old_source_map, new_source_map, "source_map 函数"
    )

    old_owner = '''            if obj in owners:
                fail(f"对象文件重复映射：{obj} ({owners[obj]} 和 {module})")
            owners[obj] = module
'''

    new_owner = '''            if bits != "user":
                if obj in owners:
                    fail(f"对象文件重复映射：{obj} ({owners[obj]} 和 {module})")
                owners[obj] = module
'''

    planner = replace_once(
        planner, old_owner, new_owner, "对象文件唯一性检查"
    )

    planner = replace_once(
        planner,
        '''    objects32 = []
    objects64 = []

    for module in order:
        for source, obj, bits in mapped.get(module, []):
            if bits == 32:
                objects32.append(obj)
            else:
                objects64.append(obj)

    return order, objects32, objects64
''',
        '''    objects32 = []
    objects64 = []
    userspace_sources = []

    for module in order:
        for source, obj, bits in mapped.get(module, []):
            if bits == 32:
                objects32.append(obj)
            elif bits == 64:
                objects64.append(obj)
            else:
                userspace_sources.append(source)

    return order, objects32, objects64, userspace_sources
''',
        "构建计划对象分类",
    )

    planner = replace_once(
        planner,
        "def write_makefile(order, objects32, objects64):",
        "def write_makefile(order, objects32, objects64, userspace_sources):",
        "Makefile 生成函数签名",
    )

    planner = replace_once(
        planner,
        '''        "OBJS_64 := " + " ".join(objects64),
        "",
''',
        '''        "OBJS_64 := " + " ".join(objects64),
        "USERSPACE_SOURCES := " + " ".join(userspace_sources),
        "",
''',
        "用户空间源码输出变量",
    )

    planner = replace_once(
        planner,
        '''        order, objects32, objects64 = make_plan(
            config, deps, args.module, mapped
        )
''',
        '''        order, objects32, objects64, userspace_sources = make_plan(
            config, deps, args.module, mapped
        )
''',
        "构建计划调用",
    )

    planner = replace_once(
        planner,
        '''            write_makefile(order, objects32, objects64)
''',
        '''            write_makefile(
                order, objects32, objects64, userspace_sources
            )
''',
        "Makefile 写入调用",
    )

    planner = replace_once(
        planner,
        '''            print(f"64 位对象文件：{len(objects64)}")
            print()
''',
        '''            print(f"64 位对象文件：{len(objects64)}")
            print(f"用户空间源码：{len(userspace_sources)}")
            print()
            if userspace_sources:
                print("用户空间源码映射：")
                for source in userspace_sources:
                    print(f"  {source}")
                print()
''',
        "构建计划显示",
    )

    updated = [conf, deps, status, planner]

    # 所有文本变更先在内存中完成，再备份和写入。
    for path in FILES:
        backup = path.with_name(path.name + ".before-foundation-" + stamp)
        shutil.copy2(path, backup)
        backups[path] = backup

    for path, content in zip(FILES, updated):
        path.write_text(content, encoding="utf-8")

    print("[OK] 四份配置/规划器文件已更新")
    print("[CHECK] Python 语法检查")

    result = subprocess.run(
        [sys.executable, "-m", "py_compile",
         str(ROOT / "tools/qbuild/module_plan.py")],
        cwd=ROOT,
    )
    if result.returncode:
        raise RuntimeError("module_plan.py Python 语法检查失败")

    print("[CHECK] QBuild 模块配置、依赖和源码映射")
    result = subprocess.run(
        [sys.executable, "tools/qbuild/module_plan.py", "--check", "--make"],
        cwd=ROOT,
    )
    if result.returncode:
        raise RuntimeError("QBuild 模块检查或 Makefile 生成失败")

    print("[OK] 基础服务模块登记成功")
    print("[BACKUP] 备份文件：")
    for backup in backups.values():
        print(" ", backup.relative_to(ROOT))

except Exception as error:
    print(f"[ERROR] {error}", file=sys.stderr)
    if backups:
        print("[ROLLBACK] 正在恢复原始文件")
        for path, backup in backups.items():
            shutil.copy2(backup, path)
    sys.exit(1)
