#!/data/data/com.termux/files/usr/bin/bash
# qmjyOS Phase 3 comprehensive audit - read-only, evidence-oriented.
# Run from any directory; all reports stay under the repository's build/ tree.
set +e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$ROOT" || exit 2

STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
OUT="build/phase3-final-audit/$STAMP"
mkdir -p "$OUT/logs"
SUMMARY="$OUT/summary.txt"
RESULTS="$OUT/results.tsv"
: > "$RESULTS"

echo "qmjyOS Phase 3 Final Audit" | tee "$SUMMARY"
echo "UTC: $STAMP" | tee -a "$SUMMARY"
echo "Repository: $ROOT" | tee -a "$SUMMARY"
echo "Commit: $(git rev-parse HEAD 2>/dev/null || echo UNKNOWN)" | tee -a "$SUMMARY"
echo "Worktree: $(if [ -n "$(git status --porcelain 2>/dev/null)" ]; then echo DIRTY; else echo CLEAN_OR_NO_GIT; fi)" | tee -a "$SUMMARY"
echo "Mode: READ-ONLY (build/test outputs are written under build/)" | tee -a "$SUMMARY"
echo | tee -a "$SUMMARY"

run_check() {
    label="$1"; shift
    safe="$(printf '%s' "$label" | tr ' /:' '___' | tr -cd 'A-Za-z0-9_.-')"
    logfile="$OUT/logs/${safe}.log"
    echo "===== $label ====="
    echo "COMMAND: $*" | tee "$logfile"
    "$@" 2>&1 | tee -a "$logfile"
    rc=${PIPESTATUS[0]}
    if [ "$rc" -eq 0 ]; then state="PASS"; else state="FAIL($rc)"; fi
    printf '%s\t%s\t%s\n' "$state" "$label" "$logfile" >> "$RESULTS"
    echo "[$state] $label"
    echo
}

run_optional() {
    label="$1"; shift
    if [ "$1" = "__SHELL__" ]; then
        shift
        run_check "$label" bash -lc "$*"
    elif [ -f "$1" ]; then
        run_check "$label" bash "$1"
    elif [ -x "$1" ]; then
        run_check "$label" "$@"
    else
        printf 'NOT_RUN\t%s\tmissing: %s\n' "$label" "$1" >> "$RESULTS"
        echo "[NOT RUN] $label (missing: $1)"
        echo
    fi
}

# 1. Toolchain/configuration and module graph.
run_check "QBuild doctor" ./qbuild doctor
run_check "QBuild self-test" ./qbuild self-test
if [ -f tools/qbuild/module_plan.py ]; then
    run_check "Module plan consistency" python3 tools/qbuild/module_plan.py --check
else
    printf 'NOT_RUN\tModule plan consistency\tmissing tools/qbuild/module_plan.py\n' >> "$RESULTS"
fi
run_check "Tracked shell/Python source checks" bash tools/ci/check_sources.sh

# 2. Both x86_64 boot targets: compile/link and their registered test suites.
run_check "BIOS complete build" ./qbuild build --arch x86_64 --boot bios --jobs 2
run_check "BIOS complete test suite" ./qbuild test --arch x86_64 --boot bios --jobs 2
run_check "UEFI complete build" ./qbuild build --arch x86_64 --boot uefi --jobs 2
run_check "UEFI test suite" ./qbuild test --arch x86_64 --boot uefi --jobs 2

# 3. Explicit format/layout and security-oriented CI checks, when present.
if [ -f tests/boot/test_uefi_elf_layout.py ] && [ -f kernel64-uefi.elf ]; then
    run_check "UEFI ELF layout" python3 tests/boot/test_uefi_elf_layout.py kernel64-uefi.elf
else
    printf 'NOT_RUN\tUEFI ELF layout\tmissing test or kernel64-uefi.elf\n' >> "$RESULTS"
fi
run_optional "ELF layout verifier" tools/ci/verify_elf_layout.sh
run_optional "Host ASan/UBSan suite" tools/ci/run_host_sanitizers.sh
run_optional "QFS crash consistency suite" tools/qfs/run_crash_tests.sh
run_optional "QEMU BIOS boot evidence test" tools/qemu_boot_test.sh
run_optional "QBoot BIOS runtime test" tools/qboot/test_bios_boot.sh

# 4. Record Phase 3 implementation/test surface. Presence is not a pass claim.
{
    echo "===== PHASE 3 SOURCE / TEST INVENTORY ====="
    for p in \
        exec/elf/elf64.c \
        kernel/core/user_access.c \
        kernel/syscall/syscall.c \
        arch/x86_64/cpu/ring3_entry.S \
        arch/x86_64/interrupts/exception.c \
        arch/x86_64/interrupts/excep.S \
        mm/paging/paging.c \
        sched/core \
        tests/exec/elf64_test.c \
        tests/syscall/syscall_test.c \
        tests/paging_host_test.c \
        tests/userspace
    do
        if [ -e "$p" ]; then echo "[PRESENT] $p"; else echo "[MISSING] $p"; fi
    done
    echo
    echo "===== USER-ACCESS / SYSCALL / EXCEPTION TEST REFERENCES ====="
    grep -R -n -E \
      'qm_user_range_check|qm_copy_from_user|qm_copy_to_user|user.?fault|page.?fault|TRAP_EXIT|syscall|USER|NX|U/S' \
      tests kernel arch/x86_64 2>/dev/null | head -n 500
} > "$OUT/phase3-inventory.txt"

# 5. Search retained QEMU logs for concrete Ring 3 runtime evidence.
{
    echo "===== RING 3 RUNTIME EVIDENCE ====="
    if [ -d build/test-evidence ]; then
        grep -R -n -E \
          'Private PML4|Private CR3|Entering CPL3|System call received|Basic privilege transition: PASS|ELF process exit.*PASS|page reclamation: PASS|Ring 3.*PASS' \
          build/test-evidence 2>/dev/null | head -n 300
    else
        echo "NO build/test-evidence directory"
    fi
} > "$OUT/ring3-evidence.txt"

# 6. Repository hygiene.
run_check "Git diff whitespace check" git --no-pager diff --check

# 7. Summarize results without converting missing evidence into success.
{
    echo
    echo "===== RESULT COUNTS ====="
    awk -F '\t' '
      $1=="PASS"{p++}
      $1 ~ /^FAIL/{f++}
      $1=="NOT_RUN"{n++}
      END{printf "PASS: %d\nFAIL: %d\nNOT RUN: %d\n",p,f,n}
    ' "$RESULTS"
    echo
    echo "===== RESULT TABLE ====="
    while IFS="$(printf '\t')" read -r state label detail; do
        printf '%-12s %s -- %s\n' "$state" "$label" "$detail"
    done < "$RESULTS"
    echo
    echo "===== PHASE 3 EVIDENCE FILES ====="
    echo "$OUT/phase3-inventory.txt"
    echo "$OUT/ring3-evidence.txt"
    echo "$OUT/logs/"
    echo
    echo "Interpretation:"
    echo "- PASS means that exact command exited 0 in this run."
    echo "- FAIL means that exact command failed; inspect its log."
    echo "- NOT RUN means unavailable/not detected; it is not a pass."
    echo "- Source/test presence and log strings are evidence pointers, not proof of full security."
    echo "- This audit does not prove SMP safety, race freedom, complete preemptive scheduling, or production-grade fault recovery unless dedicated tests exercise them."
} | tee -a "$SUMMARY"

echo
echo "AUDIT_REPORT=$SUMMARY"
echo "AUDIT_RESULTS=$RESULTS"
awk -F '\t' '$1 ~ /^FAIL/{bad=1} END{exit bad}' "$RESULTS"
