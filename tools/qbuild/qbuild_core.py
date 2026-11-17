#!/usr/bin/env python3
"""QBuild 1.0.0: stable command-line build and project-management frontend."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Iterable

VERSION = "1.0.0"
CONFIG_FILES = ("modules.conf", "modules.dep", "modules.status")
MODULE_NAME_RE = re.compile(r"^[A-Z][A-Z0-9_]*$")
SNAPSHOT_NAME_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,79}$")
DEFAULT_TEST_TARGETS = (
    "test-qbuild", "test-memory", "test-paging", "test-heap", "test-vfs", "test-ramfs",
    "test-qfs", "test-interfaces", "test-kernel-foundation", "test-elf64",
    "test-process", "test-services",
)


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds").replace("+00:00", "Z")


def safe_name(value: str, what: str) -> str:
    if not SNAPSHOT_NAME_RE.fullmatch(value) or value in {".", ".."}:
        raise ValueError(f"{what} 名称非法：{value!r}")
    return value


def read_key_values(path: Path, allowed: set[str]) -> dict[str, str]:
    values: dict[str, str] = {}
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            raise ValueError(f"{path}:{number}: 缺少 '='")
        key, value = (part.strip() for part in line.split("=", 1))
        if not MODULE_NAME_RE.fullmatch(key):
            raise ValueError(f"{path}:{number}: 非法模块名称 {key!r}")
        if key in values:
            raise ValueError(f"{path}:{number}: 重复定义 {key}")
        if value not in allowed:
            raise ValueError(f"{path}:{number}: {key} 的值无效：{value!r}")
        values[key] = value
    if not values:
        raise ValueError(f"配置文件为空：{path}")
    return values


def read_dependencies(path: Path) -> dict[str, list[str]]:
    result: dict[str, list[str]] = {}
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if ":" not in line:
            raise ValueError(f"{path}:{number}: 缺少 ':'")
        name, raw_deps = (part.strip() for part in line.split(":", 1))
        if not MODULE_NAME_RE.fullmatch(name):
            raise ValueError(f"{path}:{number}: 非法模块名称 {name!r}")
        if name in result:
            raise ValueError(f"{path}:{number}: 重复定义 {name}")
        deps = raw_deps.split() if raw_deps else []
        if len(deps) != len(set(deps)):
            raise ValueError(f"{path}:{number}: {name} 存在重复依赖")
        for dep in deps:
            if not MODULE_NAME_RE.fullmatch(dep):
                raise ValueError(f"{path}:{number}: 非法依赖名称 {dep!r}")
        result[name] = deps
    if not result:
        raise ValueError(f"依赖文件为空：{path}")
    return result


def load_configuration(root: Path) -> tuple[dict[str, str], dict[str, list[str]], dict[str, str]]:
    cfg_dir = root / "config"
    config = read_key_values(cfg_dir / "modules.conf", {"enabled", "disabled", "optional"})
    deps = read_dependencies(cfg_dir / "modules.dep")
    status = read_key_values(cfg_dir / "modules.status", {"implemented", "experimental", "planned"})
    validate_configuration(config, deps, status)
    return config, deps, status


def validate_configuration(config: dict[str, str], deps: dict[str, list[str]], status: dict[str, str]) -> None:
    names = set(config)
    if set(deps) != names:
        raise ValueError(
            "配置与依赖表模块不一致："
            f"缺少定义={sorted(names - set(deps))}，多余定义={sorted(set(deps) - names)}"
        )
    if set(status) != names:
        raise ValueError(
            "配置与状态表模块不一致："
            f"缺少状态={sorted(names - set(status))}，多余状态={sorted(set(status) - names)}"
        )
    for module, requirements in deps.items():
        for requirement in requirements:
            if requirement not in names:
                raise ValueError(f"{module} 依赖未知模块 {requirement}")

    visiting: list[str] = []
    visited: set[str] = set()

    def visit(module: str) -> None:
        if module in visiting:
            cycle = visiting[visiting.index(module):] + [module]
            raise ValueError("循环依赖：" + " -> ".join(cycle))
        if module in visited:
            return
        visiting.append(module)
        for dependency in deps[module]:
            visit(dependency)
        visiting.pop()
        visited.add(module)

    for module in sorted(names):
        visit(module)

    for module, state in config.items():
        if state == "enabled":
            for dependency in deps[module]:
                if config[dependency] != "enabled":
                    raise ValueError(
                        f"{module} 已启用，但依赖 {dependency} 的配置状态为 {config[dependency]}"
                    )


def dependency_closure(module: str, deps: dict[str, list[str]]) -> set[str]:
    if module not in deps:
        raise ValueError(f"未知模块：{module}")
    result: set[str] = set()

    def add(name: str) -> None:
        if name in result:
            return
        result.add(name)
        for dep in deps[name]:
            add(dep)

    add(module)
    return result


def enabled_dependents(module: str, config: dict[str, str], deps: dict[str, list[str]]) -> set[str]:
    if module not in config:
        raise ValueError(f"未知模块：{module}")
    reverse: dict[str, set[str]] = {name: set() for name in config}
    for child, requirements in deps.items():
        for requirement in requirements:
            reverse[requirement].add(child)
    result: set[str] = set()
    pending = [module]
    while pending:
        current = pending.pop()
        for child in reverse[current]:
            if config[child] == "enabled" and child not in result:
                result.add(child)
                pending.append(child)
    return result


def atomic_write(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + f".qbuild-tmp-{os.getpid()}")
    try:
        temporary.write_text(content, encoding="utf-8", newline="\n")
        os.replace(temporary, path)
    finally:
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass


def change_module_states(root: Path, changes: dict[str, str]) -> None:
    config_path = root / "config/modules.conf"
    original = config_path.read_text(encoding="utf-8")
    lines = original.splitlines(keepends=True)
    seen: set[str] = set()
    updated: list[str] = []
    for raw in lines:
        match = re.match(r"^(\s*)([A-Z][A-Z0-9_]*)(\s*=\s*)([^\s#]+)(.*)$", raw.rstrip("\r\n"))
        if not match:
            updated.append(raw)
            continue
        indent, module, separator, old_value, tail = match.groups()
        if module in changes:
            seen.add(module)
            newline = "\r\n" if raw.endswith("\r\n") else ("\n" if raw.endswith("\n") else "")
            updated.append(f"{indent}{module}{separator}{changes[module]}{tail}{newline}")
        else:
            updated.append(raw)
    missing = set(changes) - seen
    if missing:
        raise ValueError(f"模块配置中缺少定义：{', '.join(sorted(missing))}")

    new_content = "".join(updated)
    old_config, deps, status = load_configuration(root)
    try:
        new_config = dict(old_config)
        new_config.update(changes)
        validate_configuration(new_config, deps, status)
        atomic_write(config_path, new_content)
        # The source-map checker remains authoritative for full build configuration.
        result = subprocess.run(
            [sys.executable, str(root / "tools/qbuild/module_plan.py"), "--check"],
            cwd=root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, check=False,
        )
        if result.returncode != 0:
            raise ValueError("QBuild 模块完整性检查失败，配置未保留：\n" + result.stdout.strip())
    except Exception:
        atomic_write(config_path, original)
        raise


def create_snapshot(root: Path, name: str | None = None, *, validate: bool = True) -> Path:
    if validate:
        load_configuration(root)
    snapshots = root / "build/qbuild/snapshots"
    snapshots.mkdir(parents=True, exist_ok=True)
    if name is None:
        name = datetime.now(timezone.utc).strftime("snapshot-%Y%m%dT%H%M%S") + f"-{time.time_ns() % 1_000_000_000:09d}Z-{os.getpid()}"
    name = safe_name(name, "快照")
    destination = snapshots / name
    if destination.exists():
        raise ValueError(f"快照已存在：{name}")
    staging = snapshots / ("." + name + f".tmp-{os.getpid()}")
    staging.mkdir()
    try:
        hashes: dict[str, str] = {}
        for filename in CONFIG_FILES:
            content = (root / "config" / filename).read_bytes()
            (staging / filename).write_bytes(content)
            hashes[filename] = hashlib.sha256(content).hexdigest()
        manifest = {
            "format": 1,
            "qbuild_version": VERSION,
            "created_at": utc_now(),
            "files": hashes,
        }
        (staging / "manifest.json").write_text(
            json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
        )
        os.replace(staging, destination)
    except Exception:
        shutil.rmtree(staging, ignore_errors=True)
        raise
    return destination


def list_snapshots(root: Path) -> list[Path]:
    folder = root / "build/qbuild/snapshots"
    if not folder.is_dir():
        return []
    return sorted(
        (item for item in folder.iterdir() if item.is_dir() and not item.name.startswith(".")),
        key=lambda item: item.name,
        reverse=True,
    )


def _read_verified_snapshot(snapshot: Path) -> dict[str, bytes]:
    manifest_path = snapshot / "manifest.json"
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"快照清单无法读取：{error}") from error
    if manifest.get("format") != 1 or set(manifest.get("files", {})) != set(CONFIG_FILES):
        raise ValueError("快照格式或文件清单不受支持")
    result: dict[str, bytes] = {}
    for filename in CONFIG_FILES:
        data = (snapshot / filename).read_bytes()
        digest = hashlib.sha256(data).hexdigest()
        if digest != manifest["files"][filename]:
            raise ValueError(f"快照完整性校验失败：{filename}")
        result[filename] = data
    return result


def restore_snapshot(root: Path, name: str) -> Path:
    name = safe_name(name, "快照")
    snapshot = root / "build/qbuild/snapshots" / name
    if not snapshot.is_dir():
        raise ValueError(f"快照不存在：{name}")
    restored = _read_verified_snapshot(snapshot)
    # Validate the candidate configuration before changing the working tree.
    cfg_values = read_key_values_bytes(restored["modules.conf"], {"enabled", "disabled", "optional"}, "modules.conf")
    dep_values = read_dependencies_text(restored["modules.dep"], "modules.dep")
    status_values = read_key_values_bytes(restored["modules.status"], {"implemented", "experimental", "planned"}, "modules.status")
    validate_configuration(cfg_values, dep_values, status_values)
    # Preserve even a currently invalid configuration so a failed restore can be
    # rolled back and an operator can inspect the exact pre-restore state.
    safety = create_snapshot(root, validate=False)
    originals = {filename: (root / "config" / filename).read_bytes() for filename in CONFIG_FILES}
    try:
        for filename, content in restored.items():
            atomic_write(root / "config" / filename, content.decode("utf-8"))
        check = subprocess.run(
            [sys.executable, str(root / "tools/qbuild/module_plan.py"), "--check"],
            cwd=root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, check=False,
        )
        if check.returncode != 0:
            raise ValueError("恢复后的 QBuild 完整性检查失败：\n" + check.stdout.strip())
    except Exception:
        for filename, content in originals.items():
            atomic_write(root / "config" / filename, content.decode("utf-8"))
        raise
    return safety


def read_key_values_bytes(data: bytes, allowed: set[str], label: str) -> dict[str, str]:
    import io
    return read_key_values_from_lines(io.StringIO(data.decode("utf-8")).readlines(), label, allowed)


def read_key_values_from_lines(lines: Iterable[str], label: str, allowed: set[str]) -> dict[str, str]:
    values: dict[str, str] = {}
    for number, raw in enumerate(lines, 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            raise ValueError(f"{label}:{number}: 缺少 '='")
        key, value = (part.strip() for part in line.split("=", 1))
        if not MODULE_NAME_RE.fullmatch(key) or key in values or value not in allowed:
            raise ValueError(f"{label}:{number}: 非法或重复配置项")
        values[key] = value
    if not values:
        raise ValueError(f"{label}: 配置为空")
    return values


def read_dependencies_text(data: bytes, label: str) -> dict[str, list[str]]:
    result: dict[str, list[str]] = {}
    for number, raw in enumerate(data.decode("utf-8").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if ":" not in line:
            raise ValueError(f"{label}:{number}: 缺少 ':'")
        name, raw_deps = (part.strip() for part in line.split(":", 1))
        deps = raw_deps.split() if raw_deps else []
        if not MODULE_NAME_RE.fullmatch(name) or name in result or len(deps) != len(set(deps)):
            raise ValueError(f"{label}:{number}: 非法或重复依赖项")
        if any(not MODULE_NAME_RE.fullmatch(dep) for dep in deps):
            raise ValueError(f"{label}:{number}: 依赖名称非法")
        result[name] = deps
    if not result:
        raise ValueError(f"{label}: 依赖表为空")
    return result


def get_git_metadata(root: Path) -> dict[str, str | bool | None]:
    def git(*args: str) -> str | None:
        try:
            value = subprocess.check_output(["git", *args], cwd=root, stderr=subprocess.DEVNULL, text=True).strip()
            return value or None
        except (OSError, subprocess.CalledProcessError):
            return None
    return {"commit": git("rev-parse", "HEAD"), "dirty": bool(git("status", "--porcelain")) if git("rev-parse", "--is-inside-work-tree") else None}


def write_json_atomic(path: Path, value: dict) -> None:
    atomic_write(path, json.dumps(value, ensure_ascii=False, indent=2) + "\n")


def run_tracked(root: Path, kind: str, command: list[str], context: dict) -> int:
    report_dir = root / "build/qbuild/reports"
    report_dir.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S") + f"{time.time_ns() % 1_000_000_000:09d}Z"
    stem = f"{stamp}-{os.getpid()}-{kind}"
    log_path = report_dir / (stem + ".log")
    report_path = report_dir / (stem + ".json")
    started = time.monotonic()
    started_at = utc_now()
    return_code = 127
    launch_error = None
    print("===== QBuild v1.0.0：" + kind + " =====", flush=True)
    print("命令：" + " ".join(command), flush=True)
    process = None
    try:
        with log_path.open("w", encoding="utf-8") as log:
            try:
                process = subprocess.Popen(
                    command, cwd=root, stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT, text=True, errors="replace", bufsize=1,
                )
                assert process.stdout is not None
                with process.stdout as stream:
                    for line in stream:
                        sys.stdout.write(line)
                        sys.stdout.flush()
                        log.write(line)
                return_code = process.wait()
            except OSError as error:
                launch_error = str(error)
                log.write("QBuild launch error: " + launch_error + "\n")
                print("QBuild 启动命令失败：" + launch_error, file=sys.stderr)
    except KeyboardInterrupt:
        return_code = 130
        launch_error = "interrupted by user"
        if process is not None and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        print("\n收到中断信号，已记录本次构建/测试。", file=sys.stderr)
    finished_at = utc_now()
    report = {
        "schema": 1,
        "qbuild_version": VERSION,
        "kind": kind,
        "status": "passed" if return_code == 0 else ("interrupted" if return_code == 130 else "failed"),
        "return_code": return_code,
        "started_at": started_at,
        "finished_at": finished_at,
        "duration_seconds": round(time.monotonic() - started, 3),
        "command": command,
        "context": context,
        "git": get_git_metadata(root),
        "host": {"platform": platform.platform(), "python": platform.python_version()},
        "log_file": str(log_path.relative_to(root)),
        "launch_error": launch_error,
    }
    write_json_atomic(report_path, report)
    print(f"\nQBuild 报告：{report_path.relative_to(root)}")
    print(f"QBuild 日志：{log_path.relative_to(root)}")
    print("QBuild 结果：" + report["status"].upper())
    return return_code


def validate_target(arch: str, boot: str, jobs: int) -> None:
    if arch not in {"x86_64", "aarch64"}:
        raise ValueError(f"不支持的架构名称：{arch}")
    if boot not in {"bios", "uefi"}:
        raise ValueError(f"不支持的启动方式：{boot}")
    if arch != "x86_64":
        raise RuntimeError(f"目标尚未实现：{arch} + {boot}；当前仅支持 x86_64 + bios/uefi")
    if jobs < 1:
        raise ValueError("并行任务数必须是正整数")


def module_tool(root: Path, *args: str, capture: bool = False):
    command = [sys.executable, str(root / "tools/qbuild/module_plan.py"), *args]
    return subprocess.run(command, cwd=root, text=True, check=False,
                          stdout=subprocess.PIPE if capture else None,
                          stderr=subprocess.STDOUT if capture else None)


def command_build(root: Path, args: argparse.Namespace, selected: list[str]) -> int:
    validate_target(args.arch, args.boot, args.jobs)
    context = {"arch": args.arch, "boot": args.boot, "jobs": args.jobs, "modules": selected or "all"}
    preflight = run_tracked(
        root, "preflight", [sys.executable, str(root / "tools/qbuild/module_plan.py"), "--check"], context
    )
    if preflight != 0:
        return preflight
    if args.boot == "uefi":
        if selected and any(module not in {"QBOOT", "QBOOT_UEFI"} for module in selected):
            raise ValueError("UEFI 启动目标需要完整内核映像；请勿使用局部内核模块选择")
        plan_result = run_tracked(
            root, "plan", [sys.executable, str(root / "tools/qbuild/module_plan.py"), "--make"], context
        )
        if plan_result != 0:
            return plan_result
        return run_tracked(
            root, "build", ["make", f"-j{args.jobs}", "qboot-uefi"], context
        )
    if not selected:
        plan_result = run_tracked(
            root, "plan", [sys.executable, str(root / "tools/qbuild/module_plan.py"), "--make"], context
        )
        if plan_result != 0:
            return plan_result
        command = ["make", f"-j{args.jobs}", "all"]
    else:
        for module in selected:
            if not MODULE_NAME_RE.fullmatch(module):
                raise ValueError(f"模块名称非法：{module}")
        objects = module_tool(root, "--objects", *sum((["--module", m] for m in selected), []), capture=True)
        if objects.returncode:
            sys.stdout.write(objects.stdout or "")
            return objects.returncode
        object_targets = [line.strip() for line in (objects.stdout or "").splitlines() if line.strip()]
        if not object_targets:
            raise ValueError("所选模块没有可构建的内核对象文件")
        command = ["make", f"-j{args.jobs}", *object_targets]
        print("选中对象文件：\n" + "\n".join(object_targets))
    return run_tracked(root, "build", command, context)


def command_test(root: Path, args: argparse.Namespace) -> int:
    validate_target(args.arch, args.boot, args.jobs)
    # Host-side tests are shared; boot-runtime tests are firmware-specific.
    targets = list(DEFAULT_TEST_TARGETS)
    if args.boot == "bios":
        targets.append("test-boot")
    elif args.boot == "uefi":
        targets.append("test-qboot-uefi")
    context = {"arch": args.arch, "boot": args.boot, "jobs": args.jobs, "targets": targets}
    preflight = run_tracked(
        root, "preflight", [sys.executable, str(root / "tools/qbuild/module_plan.py"), "--check"], context
    )
    if preflight != 0:
        return preflight
    command = ["make", f"-j{args.jobs}", *targets]
    return run_tracked(root, "test", command, context)


def command_enable(root: Path, module: str, with_deps: bool) -> None:
    config, deps, _ = load_configuration(root)
    if module not in config:
        raise ValueError(f"未知模块：{module}")
    affected = dependency_closure(module, deps) if with_deps else {module}
    missing = sorted(dep for dep in deps[module] if config[dep] != "enabled")
    if missing and not with_deps:
        raise ValueError(
            f"不能单独启用 {module}；以下直接依赖尚未启用：{', '.join(missing)}。"
            "使用 --with-deps 可一并启用依赖。"
        )
    changes = {name: "enabled" for name in affected if config[name] != "enabled"}
    if not changes:
        print(f"模块 {module} 已启用，无需修改。")
        return
    change_module_states(root, changes)
    print("已启用模块：" + ", ".join(sorted(changes)))


def command_disable(root: Path, module: str, cascade: bool) -> None:
    config, deps, _ = load_configuration(root)
    if module not in config:
        raise ValueError(f"未知模块：{module}")
    dependents = enabled_dependents(module, config, deps)
    if dependents and not cascade:
        raise ValueError(
            f"不能禁用 {module}；仍有启用模块依赖它：{', '.join(sorted(dependents))}。"
            "使用 --cascade 可一并禁用这些依赖方。"
        )
    changes = {name: "disabled" for name in dependents | {module} if config[name] != "disabled"}
    if not changes:
        print(f"模块 {module} 已禁用，无需修改。")
        return
    change_module_states(root, changes)
    print("已禁用模块：" + ", ".join(sorted(changes)))


def command_doctor(root: Path, strict: bool) -> int:
    required = ("python3", "make", "clang", "ld.lld", "llvm-objcopy")
    optional = ("qemu-system-x86_64", "git")
    failed = False
    print("QBuild v1.0.0 环境诊断")
    py_ok = sys.version_info >= (3, 10)
    print(f"{'PASS' if py_ok else 'FAIL'}  Python version: {platform.python_version()} (requires >= 3.10)")
    failed = not py_ok
    for name in required:
        path = shutil.which(name)
        print(f"{'PASS' if path else 'FAIL'}  required {name}: {path or 'not found'}")
        failed = failed or path is None
    for name in optional:
        path = shutil.which(name)
        print(f"{'FOUND' if path else 'OPTIONAL'} optional {name}: {path or 'not found'}")
        if strict and path is None:
            failed = True
    config = module_tool(root, "--check", capture=True)
    print("PASS  module configuration" if config.returncode == 0 else "FAIL  module configuration")
    if config.stdout:
        print(config.stdout.rstrip())
    failed = failed or config.returncode != 0
    return 1 if failed else 0


def command_report(root: Path, action: str, target: str | None) -> int:
    folder = root / "build/qbuild/reports"
    reports = sorted(folder.glob("*.json"), key=lambda p: p.name, reverse=True) if folder.exists() else []
    if action == "list":
        if not reports:
            print("暂无构建/测试报告。")
            return 0
        for path in reports:
            try:
                data = json.loads(path.read_text(encoding="utf-8"))
                print(f"{data.get('status','unknown').upper():12} {data.get('kind','?'):8} {data.get('finished_at','?'):25} {path.name}")
            except (OSError, json.JSONDecodeError):
                print(f"INVALID      {path.name}")
        return 0
    if not target:
        raise ValueError("report show 需要报告文件名或不带扩展名的 ID")
    safe_name(target, "报告")
    path = folder / (target if target.endswith(".json") else target + ".json")
    if not path.is_file():
        raise ValueError(f"报告不存在：{path.name}")
    print(path.read_text(encoding="utf-8"), end="")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="qbuild", description="qmjyOS QBuild 1.0.0")
    parser.add_argument("--version", action="version", version=f"QBuild {VERSION}")
    sub = parser.add_subparsers(dest="command")
    sub.add_parser("help", help="显示帮助")
    sub.add_parser("version", help="显示版本")
    info = sub.add_parser("info", help="显示版本和目标信息")
    info.add_argument("--arch", default="x86_64")
    info.add_argument("--boot", default="bios")
    info.add_argument("--jobs", type=int, default=2)
    info.add_argument("--module", action="append", default=[])

    config = sub.add_parser("config", help="校验、快照和恢复配置")
    config.add_argument("action", nargs="?", choices=("validate", "snapshot", "snapshots", "restore"), default="validate")
    config.add_argument("name", nargs="?")
    config.add_argument("--arch", default="x86_64")
    config.add_argument("--boot", default="bios")
    config.add_argument("--jobs", type=int, default=2)
    config.add_argument("--module", action="append", default=[])

    for legacy_name, legacy_help in (("modules", "列出模块状态"), ("deps", "显示依赖表"), ("plan", "显示构建计划")):
        legacy = sub.add_parser(legacy_name, help=legacy_help)
        legacy.add_argument("--arch", default="x86_64")
        legacy.add_argument("--boot", default="bios")
        legacy.add_argument("--jobs", type=int, default=2)
        legacy.add_argument("--module", action="append", default=[])

    module = sub.add_parser("module", help="安全管理模块配置")
    module_sub = module.add_subparsers(dest="module_action", required=True)
    enable = module_sub.add_parser("enable", help="启用模块")
    enable.add_argument("name")
    enable.add_argument("--with-deps", action="store_true")
    disable = module_sub.add_parser("disable", help="禁用模块")
    disable.add_argument("name")
    disable.add_argument("--cascade", action="store_true")
    show = module_sub.add_parser("show", help="查看单个模块")
    show.add_argument("name")

    for name, help_text in (("build", "构建完整内核或指定模块"), ("test", "运行完整测试套件")):
        cmd = sub.add_parser(name, help=help_text)
        cmd.add_argument("--arch", default="x86_64")
        cmd.add_argument("--boot", default="bios")
        cmd.add_argument("--jobs", type=int, default=int(os.environ.get("QBUILD_JOBS", "2")))
        cmd.add_argument("--module", action="append", default=[])

    clean = sub.add_parser("clean", help="调用 Makefile 清理构建产物")
    clean.add_argument("--arch", default="x86_64")
    clean.add_argument("--boot", default="bios")
    clean.add_argument("--jobs", type=int, default=2)
    clean.add_argument("--module", action="append", default=[])
    doctor = sub.add_parser("doctor", help="检查工具链和项目配置")
    doctor.add_argument("--strict", action="store_true", help="同时要求可选工具存在")
    sub.add_parser("self-test", help="运行 QBuild 自身回归测试")

    report = sub.add_parser("report", help="查看构建/测试报告")
    report_sub = report.add_subparsers(dest="report_action", required=True)
    report_sub.add_parser("list", help="列出报告")
    show_report = report_sub.add_parser("show", help="显示一份 JSON 报告")
    show_report.add_argument("target")
    return parser


def main(argv: list[str] | None = None) -> int:
    root = Path(__file__).resolve().parents[2]
    parser = build_parser()
    args = parser.parse_args(argv)
    command = args.command or "help"
    try:
        if command == "help":
            parser.print_help()
            return 0
        if command == "version":
            print(f"QBuild {VERSION}")
            return 0
        if command == "info":
            print(f"qmjyOS QBuild v{VERSION}\n当前内核目标：x86_64 + BIOS\n模块配置：config/modules.conf\n依赖关系：config/modules.dep\n实现状态：config/modules.status\n报告目录：build/qbuild/reports\n快照目录：build/qbuild/snapshots")
            return 0
        if command == "config":
            if args.action in ("validate", None):
                validate_target(args.arch, args.boot, args.jobs)
                result = module_tool(root, "--check", capture=True)
                if result.stdout:
                    print(result.stdout, end="")
                return result.returncode
            if args.action == "snapshot":
                path = create_snapshot(root, args.name)
                print(f"配置快照已创建：{path.relative_to(root)}")
                return 0
            if args.action == "snapshots":
                snapshots = list_snapshots(root)
                if not snapshots:
                    print("暂无配置快照。")
                for item in snapshots:
                    manifest = json.loads((item / "manifest.json").read_text(encoding="utf-8"))
                    print(f"{item.name}  {manifest.get('created_at','unknown')}")
                return 0
            if args.action == "restore":
                if not args.name:
                    raise ValueError("config restore 需要快照名称")
                safety = restore_snapshot(root, args.name)
                print(f"配置已恢复。恢复前自动保存的快照：{safety.name}")
                return 0
        if command in ("modules", "deps", "plan"):
            flag = {"modules": "--modules", "deps": "--deps", "plan": "--show"}[command]
            result = module_tool(root, "--check", flag)
            return result.returncode
        if command == "module":
            config, deps, status = load_configuration(root)
            if args.module_action == "enable":
                command_enable(root, args.name, args.with_deps)
            elif args.module_action == "disable":
                command_disable(root, args.name, args.cascade)
            elif args.module_action == "show":
                if args.name not in config:
                    raise ValueError(f"未知模块：{args.name}")
                print(f"模块：{args.name}\n配置：{config[args.name]}\n实现状态：{status[args.name]}\n依赖：{', '.join(deps[args.name]) or '(none)'}")
            return 0
        if command == "build":
            return command_build(root, args, args.module)
        if command == "test":
            return command_test(root, args)
        if command == "clean":
            return subprocess.run(["make", "clean"], cwd=root, check=False).returncode
        if command == "doctor":
            return command_doctor(root, args.strict)
        if command == "self-test":
            tests = root / "tests/qbuild/test_qbuild_core.py"
            return subprocess.run([sys.executable, str(tests)], cwd=root, check=False).returncode
        if command == "report":
            return command_report(root, args.report_action, getattr(args, "target", None))
    except RuntimeError as error:
        print(f"QBuild 错误：{error}", file=sys.stderr)
        return 3
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"QBuild 错误：{error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
