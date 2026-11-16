#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)"
cd "$ROOT"

READELF="${LLVM_READELF:-llvm-readelf}"

command -v "$READELF" >/dev/null 2>&1 || {
    echo "[ELF FAIL] llvm-readelf not found" >&2
    exit 1
}

for file in kernel.elf kernel64.elf; do
    [ -s "$file" ] || {
        echo "[ELF FAIL] Missing or empty: $file" >&2
        exit 1
    }
done

check_elf() {
    local file="$1"
    local class="$2"
    local machine_regex="$3"
    local entry="$4"

    local header
    local program_headers

    header="$("$READELF" -h "$file")"
    program_headers="$("$READELF" -l "$file")"

    printf '%s\n' "$header" |
        grep -Eqi "Class:[[:space:]]+$class" || {
            echo "[ELF FAIL] $file: unexpected ELF class" >&2
            exit 1
        }

    printf '%s\n' "$header" |
        grep -Eqi "Machine:.*($machine_regex)" || {
            echo "[ELF FAIL] $file: unexpected machine architecture" >&2
            exit 1
        }

    printf '%s\n' "$header" |
        grep -Eqi "Entry point address:[[:space:]]*0x0*$entry" || {
            echo "[ELF FAIL] $file: unexpected entry point" >&2
            exit 1
        }

    printf '%s\n' "$program_headers" |
        grep -Eq '^[[:space:]]*LOAD[[:space:]]' || {
            echo "[ELF FAIL] $file: no PT_LOAD segment" >&2
            exit 1
        }

    echo "[ELF PASS] $file: $class, machine and entry verified"
    echo "[ELF PASS] $file: loadable segment exists"
}

check_elf kernel.elf ELF32 'Intel 80386|EM_386' 10000c
check_elf kernel64.elf ELF64 'Advanced Micro Devices X86-64|EM_X86_64' 200000

echo "[ELF PASS] Kernel ELF layout checks completed."
