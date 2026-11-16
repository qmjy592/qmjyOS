#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

echo "===== CI: tracked shell scripts ====="

while IFS= read -r -d '' file; do
    [ -f "$file" ] || continue
    first_line="$(head -n 1 "$file" || true)"

    case "$first_line" in
        '#!/bin/sh'|'#!/usr/bin/env sh'|*'/sh')
            echo "[SH] $file"
            sh -n "$file"
            ;;
        *)
            echo "[BASH] $file"
            bash -n "$file"
            ;;
    esac
done < <(git ls-files -z -- '*.sh' tools/qbuild/qbuild)

echo "===== CI: tracked Python scripts ====="

mkdir -p build/ci-pycache

PYTHONPYCACHEPREFIX=build/ci-pycache python3 - <<'PY'
import pathlib
import py_compile
import subprocess

raw = subprocess.check_output(
    ["git", "ls-files", "-z", "--", "*.py"]
)

files = [
    pathlib.Path(item.decode())
    for item in raw.split(b"\0")
    if item
]

for path in files:
    if not path.is_file():
        raise SystemExit(f"[FAIL] Missing tracked Python file: {path}")

    print(f"[PYTHON] {path}")
    py_compile.compile(str(path), doraise=True)

print(f"[PASS] Python syntax: {len(files)} files")
PY

echo "[CI PASS] Tracked shell and Python syntax checks passed."
