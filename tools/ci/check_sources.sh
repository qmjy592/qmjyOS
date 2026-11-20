#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)"
cd "$ROOT"

echo "===== CI: maintained shell and Python syntax ====="
mkdir -p build/ci-pycache

python3 - <<'PY'
import pathlib
import py_compile
import subprocess
import sys

ROOT = pathlib.Path.cwd()
ARCHIVE_PREFIXES = (
    "docs/history/legacy-tools/",
    "docs/development/repair-history/",
)

tracked_shell = subprocess.check_output(
    ["git", "ls-files", "-z", "--", "*.sh", "tools/qbuild/qbuild"]
).split(b"\0")
tracked_python = subprocess.check_output(
    ["git", "ls-files", "-z", "--", "*.py"]
).split(b"\0")

shell_files = [
    pathlib.Path(item.decode()) for item in tracked_shell if item
    and not item.decode().startswith(ARCHIVE_PREFIXES)
]
python_files = [
    pathlib.Path(item.decode()) for item in tracked_python if item
    and not item.decode().startswith(ARCHIVE_PREFIXES)
]

errors = []
for path in shell_files:
    if not path.is_file():
        errors.append(f"[FAIL] Missing tracked shell file: {path}")
        continue
    first = path.read_text(encoding="utf-8", errors="replace").splitlines()[:1]
    shebang = first[0] if first else ""
    shell = "sh" if shebang in ("#!/bin/sh", "#!/usr/bin/env sh") or shebang.endswith("/sh") else "bash"
    print(f"[{shell.upper()}] {path}")
    result = subprocess.run([shell, "-n", str(path)], text=True, capture_output=True)
    if result.returncode or result.stderr.strip():
        errors.append(f"[FAIL] {path}: syntax check failed\n{result.stderr}{result.stdout}")

for path in python_files:
    if not path.is_file():
        errors.append(f"[FAIL] Missing tracked Python file: {path}")
        continue
    print(f"[PYTHON] {path}")
    try:
        py_compile.compile(str(path), doraise=True, cfile=str(ROOT / "build/ci-pycache" / (path.as_posix().replace('/', '__') + ".pyc")))
    except py_compile.PyCompileError as exc:
        errors.append(str(exc))

if errors:
    print("\n".join(errors), file=sys.stderr)
    raise SystemExit(1)
print(f"[PASS] Maintained shell syntax: {len(shell_files)} files")
print(f"[PASS] Maintained Python syntax: {len(python_files)} files")
PY

# QBuild itself is part of the maintained shell set and must be executable.
sh -n tools/qbuild/qbuild

echo "[CI PASS] Maintained shell and Python syntax checks passed."
