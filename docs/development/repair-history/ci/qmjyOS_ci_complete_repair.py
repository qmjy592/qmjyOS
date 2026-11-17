#!/usr/bin/env python3
"""Apply the qmjyOS BIOS/UEFI CI separation and failure-evidence repair.

Place this file in the qmjyOS repository root and run it with Python 3.
The installer is fail-closed, makes timestamped backups under build/, stages
all edits before replacing files, runs QBuild's own regression tests, and
restores the edited files if validation fails. It never commits or pushes.
"""
from __future__ import annotations

import argparse
import datetime as _dt
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
from typing import Dict, List, Tuple

ROOT = Path(__file__).resolve().parent
TARGETS = (
    "tools/qbuild/qbuild_core.py",
    "tests/qbuild/test_qbuild_core.py",
    "tools/ci/run_host_sanitizers.sh",
    "tools/qfs/run_crash_tests.sh",
    "tools/qboot/test_qfs_ide.sh",
    ".github/workflows/c-cpp.yml",
    "Makefile",
)
EVIDENCE_PATHS = (
    "build/qbuild/reports/**",
    "build/test-evidence/host-sanitizers/**",
    "build/qfs-crash-tests/**",
    "build/test-evidence/qboot-bios/**",
    "build/test-evidence/qboot-uefi/**",
    "build/test-evidence/qemu-boot/**",
    "build/phase12-unified/qfs-ide-*/runtime/**",
)
TEST_METHOD = '''    def test_firmware_specific_test_selection(self):
        self.assertNotIn("test-boot", q.DEFAULT_TEST_TARGETS)
        self.assertNotIn("test-qboot-uefi", q.DEFAULT_TEST_TARGETS)

        for boot, expected, unexpected in (
            ("bios", "test-boot", "test-qboot-uefi"),
            ("uefi", "test-qboot-uefi", "test-boot"),
        ):
            with self.subTest(boot=boot):
                calls = []

                def capture(root, label, command, context):
                    calls.append((label, list(command), dict(context)))
                    return 0

                args = q.build_parser().parse_args([
                    "test", "--arch", "x86_64", "--boot", boot, "--jobs", "2"
                ])
                with patch.object(q, "run_tracked", side_effect=capture):
                    self.assertEqual(q.command_test(self.root, args), 0)

                self.assertEqual([call[0] for call in calls], ["preflight", "test"])
                selected = calls[1][1]
                self.assertIn(expected, selected)
                self.assertNotIn(unexpected, selected)
                self.assertIn(expected, calls[1][2]["targets"])
                self.assertNotIn(unexpected, calls[1][2]["targets"])

'''


def die(message: str, code: int = 2) -> None:
    print(f"[QMJYOS CI REPAIR ABORT] {message}", file=sys.stderr)
    raise SystemExit(code)


def read_text(root: Path, rel: str) -> str:
    path = root / rel
    if not path.is_file():
        die(f"Required file is missing: {rel}")
    try:
        return path.read_text(encoding="utf-8")
    except UnicodeError as exc:
        die(f"Cannot decode {rel} as UTF-8: {exc}")


def patch_qbuild_core(source: str) -> Tuple[str, bool]:
    m = re.search(r"(?ms)^DEFAULT_TEST_TARGETS\s*=\s*\(\n(?P<body>.*?)^\)", source)
    if not m:
        die("Cannot locate DEFAULT_TEST_TARGETS tuple in qbuild_core.py")
    body = m.group("body")
    boot_items = re.findall(r'"test-boot"', body)
    uefi_items = re.findall(r'"test-qboot-uefi"', body)
    if len(boot_items) > 1 or len(uefi_items) > 1:
        die("Duplicate firmware-specific target in DEFAULT_TEST_TARGETS")
    # Targets may share a line (the current tuple groups test-process,
    # test-services and test-boot together), so remove by token, not line.
    if len(boot_items) == 1:
        body = re.sub(r'"test-boot",?', "", body, count=1)
    if len(uefi_items) == 1:
        body = re.sub(r'"test-qboot-uefi",?', "", body, count=1)
    source = source[:m.start("body")] + body + source[m.end("body"):]

    old = '''    targets = list(DEFAULT_TEST_TARGETS)
    if args.boot == "uefi":
        targets.append("test-qboot-uefi")
'''
    new = '''    # Host-side tests are shared; boot-runtime tests are firmware-specific.
    targets = list(DEFAULT_TEST_TARGETS)
    if args.boot == "bios":
        targets.append("test-boot")
    elif args.boot == "uefi":
        targets.append("test-qboot-uefi")
'''
    if source.count(new) == 1:
        return source, source != read_original_qbuild
    if source.count(old) != 1:
        die("command_test() no longer matches the reviewed BIOS/UEFI target-selection shape")
    source = source.replace(old, new, 1)
    return source, True


# Set by main() immediately before source transformations; used only to report
# idempotent status without maintaining a second copy of the original content.
read_original_qbuild = ""


def patch_qbuild_tests(source: str) -> Tuple[str, bool]:
    changed = False
    if "from unittest.mock import patch" not in source:
        if source.count("import unittest\n") != 1:
            die("Cannot safely add unittest.mock import to test_qbuild_core.py")
        source = source.replace("import unittest\n", "import unittest\nfrom unittest.mock import patch\n", 1)
        changed = True

    if "def test_firmware_specific_test_selection(self):" not in source:
        anchor = re.search(r'(?m)^if __name__ == "__main__":', source)
        if not anchor or len(re.findall(r'(?m)^if __name__ == "__main__":', source)) != 1:
            die("Cannot find unique test_qbuild_core.py insertion point")
        source = source[:anchor.start()].rstrip() + "\n\n" + TEST_METHOD + source[anchor.start():]
        changed = True
    return source, changed


def add_artifact_evidence(step: str) -> Tuple[str, bool]:
    if "uses: actions/upload-artifact@v4" not in step:
        return step, False
    original = step
    step = re.sub(r"(?m)^(\s*)if:\s*success\(\)\s*$", r"\1if: always()", step, count=1)
    step = re.sub(r"(?m)^(\s*)if-no-files-found:\s*error\s*$", r"\1if-no-files-found: warn", step, count=1)

    lines = step.splitlines(keepends=True)
    path_index = next((i for i, line in enumerate(lines)
                       if re.match(r"^\s*path:\s*\|\s*(?:#.*)?$", line.rstrip("\r\n"))), None)
    if path_index is None:
        die("An upload-artifact step has no multiline path block; refusing to rewrite it")
    path_indent = len(lines[path_index]) - len(lines[path_index].lstrip(" "))
    item_indent = " " * (path_indent + 2)
    end = path_index + 1
    present = set()
    while end < len(lines):
        raw = lines[end].rstrip("\r\n")
        if raw.strip() and len(raw) - len(raw.lstrip(" ")) < path_indent + 2:
            break
        if raw.strip():
            present.add(raw.strip())
        end += 1
    additions = [p for p in EVIDENCE_PATHS if p not in present]
    if additions:
        lines[end:end] = [f"{item_indent}{p}\n" for p in additions]
    step = "".join(lines)
    return step, step != original


def patch_workflow(source: str) -> Tuple[str, bool]:
    required_jobs = ("code-and-config", "build-and-test", "uefi-build-and-test")
    for job in required_jobs:
        if not re.search(rf"(?m)^  {re.escape(job)}:\s*$", source):
            die(f"Workflow is missing expected job: {job}")

    artifact_count = source.count("uses: actions/upload-artifact@v4")
    if artifact_count < 2:
        die(f"Expected BIOS and UEFI artifact upload steps; found {artifact_count}")

    # Split at step boundaries, preserving all YAML text. Only artifact steps
    # are modified; build/test commands and their ordering remain unchanged.
    pieces = re.split(r"(?m)(?=^      - name: )", source)
    changed = False
    rebuilt: List[str] = []
    seen_artifacts = 0
    for piece in pieces:
        if "uses: actions/upload-artifact@v4" in piece:
            seen_artifacts += 1
            piece, did = add_artifact_evidence(piece)
            changed = changed or did
        rebuilt.append(piece)
    source = "".join(rebuilt)
    if seen_artifacts != artifact_count:
        die("Could not identify every upload-artifact step boundary in workflow")

    # The UEFI job must not describe its firmware-specific test as a BIOS run.
    job_start = re.search(r"(?m)^  uefi-build-and-test:\s*$", source)
    assert job_start is not None
    following_job = re.search(r"(?m)^  [A-Za-z0-9_-]+:\s*$", source[job_start.end():])
    job_end = job_start.end() + following_job.start() if following_job else len(source)
    uefi_section = source[job_start.start():job_end]
    fixed_section = re.sub(
        r"(?m)^(\s*- name: ).*(?:BIOS regression|BIOS.*UEFI OVMF).*$",
        r"\1Run UEFI OVMF runtime tests",
        uefi_section,
    )
    if fixed_section != uefi_section:
        source = source[:job_start.start()] + fixed_section + source[job_end:]
        changed = True

    # Assert that each firmware path still invokes the corresponding QBuild test.
    bios_start = re.search(r"(?m)^  build-and-test:\s*$", source)
    uefi_start = re.search(r"(?m)^  uefi-build-and-test:\s*$", source)
    if not bios_start or not uefi_start:
        die("Could not re-locate BIOS/UEFI workflow jobs after patching")
    next_after_uefi = re.search(r"(?m)^  [A-Za-z0-9_-]+:\s*$", source[uefi_start.end():])
    uefi_end = uefi_start.end() + next_after_uefi.start() if next_after_uefi else len(source)
    bios_end = uefi_start.start()
    bios_section = source[bios_start.start():bios_end]
    uefi_section = source[uefi_start.start():uefi_end]
    if "--boot bios" not in bios_section or "qbuild test" not in bios_section:
        die("BIOS job no longer contains its QBuild BIOS test invocation")
    if "--boot uefi" not in uefi_section or "qbuild test" not in uefi_section:
        die("UEFI job no longer contains its QBuild UEFI test invocation")
    return source, changed



def patch_host_sanitizers(source: str) -> Tuple[str, bool]:
    marker = "# QMJYOS_CI_EVIDENCE_LOG_V1"
    if marker in source:
        return source, False
    anchor = 'cd "$ROOT"\n'
    if source.count(anchor) != 1:
        die("Cannot safely locate repository-root setup in run_host_sanitizers.sh")
    block = '''cd "$ROOT"

# QMJYOS_CI_EVIDENCE_LOG_V1
command -v tee >/dev/null 2>&1 || {
    echo "[SANITIZER FAIL] tee is required to retain the test log." >&2
    exit 1
}
RUN_ID="${HOST_SANITIZER_RUN_ID:-$(date +%Y%m%d-%H%M%S)-$$}"
case "$RUN_ID" in
    ""|*[!A-Za-z0-9._-]*)
        echo "[SANITIZER FAIL] Invalid HOST_SANITIZER_RUN_ID: $RUN_ID" >&2
        exit 1
        ;;
esac
EVIDENCE_DIR="build/test-evidence/host-sanitizers"
mkdir -p "$EVIDENCE_DIR"
LOG_FILE="$EVIDENCE_DIR/run-$RUN_ID.log"
[ ! -e "$LOG_FILE" ] || {
    echo "[SANITIZER FAIL] Refusing to overwrite evidence log: $LOG_FILE" >&2
    exit 1
}
exec > >(tee -a "$LOG_FILE") 2>&1
echo "[SANITIZER] Evidence log: $LOG_FILE"
'''
    return source.replace(anchor, block, 1), True


def patch_qfs_crash_log(source: str) -> Tuple[str, bool]:
    marker = "# QMJYOS_CI_EVIDENCE_LOG_V1"
    if marker in source:
        return source, False
    id_line = 'RUN_ID="${QFS_TEST_RUN_ID:-$(date +%Y%m%d-%H%M%S)}"\n'
    if source.count(id_line) != 1:
        die("Cannot safely locate QFS_TEST_RUN_ID assignment")
    id_block = id_line + '''case "$RUN_ID" in
    ""|.|..|*[!A-Za-z0-9._-]*)
        echo "[QFS TEST FAIL] Invalid QFS_TEST_RUN_ID: $RUN_ID" >&2
        exit 1
        ;;
esac
'''
    source = source.replace(id_line, id_block, 1)
    anchor = 'mkdir -p "$BUILD_DIR"\n'
    if source.count(anchor) != 1:
        die("Cannot safely locate QFS crash-test build directory setup")
    block = '''mkdir -p "$BUILD_DIR"

# QMJYOS_CI_EVIDENCE_LOG_V1
command -v tee >/dev/null 2>&1 || {
    echo "[QFS TEST FAIL] tee is required to retain the test log." >&2
    exit 1
}
LOG_FILE="$BUILD_DIR/run.log"
exec > >(tee -a "$LOG_FILE") 2>&1
echo "[QFS TEST] Evidence log: $LOG_FILE"
'''
    return source.replace(anchor, block, 1), True


def patch_qfs_ide_id_validation(source: str) -> Tuple[str, bool]:
    marker = "# QFS_ID_RUN_ID_VALIDATION_V1"
    if marker in source:
        return source, False
    assignment = 'RUN_ID="${QFS_IDE_RUN_ID:-$(date +%Y%m%d-%H%M%S)-$$}"\n'
    if source.count(assignment) != 1:
        die("Cannot safely locate QFS_IDE_RUN_ID assignment in test_qfs_ide.sh")
    block = assignment + '''# QFS_ID_RUN_ID_VALIDATION_V1
case "$RUN_ID" in
    ""|.|..|*[!A-Za-z0-9._-]*)
        echo "[QFS IDE TEST FAIL] Invalid QFS_IDE_RUN_ID: $RUN_ID" >&2
        exit 1
        ;;
esac
'''
    return source.replace(assignment, block, 1), True


def verify_makefile(makefile: str) -> None:
    def target_block(name: str) -> str:
        m = re.search(rf"(?m)^{re.escape(name)}:\s*[^\n]*\n", makefile)
        if not m:
            die(f"Makefile target is missing: {name}")
        nxt = re.search(r"(?m)^[A-Za-z0-9_.-]+[^\n]*:\s*[^=\n]*$", makefile[m.end():])
        end = m.end() + nxt.start() if nxt else len(makefile)
        return makefile[m.start():end]

    bios = target_block("test-boot")
    if "tools/qboot/test_bios_boot.sh" not in bios or "tools/qboot/test_qfs_ide.sh" not in bios:
        die("Makefile test-boot must retain both QBoot BIOS runtime and QFS IDE persistence tests")
    uefi = target_block("test-qboot-uefi")
    if "tools/qboot/test_uefi_boot.sh" not in uefi:
        die("Makefile test-qboot-uefi does not invoke the OVMF runtime test")


def run_validation(root: Path) -> None:
    commands = [
        [sys.executable, "-m", "py_compile", "tools/qbuild/qbuild_core.py", "tests/qbuild/test_qbuild_core.py"],
        ["bash", "-n", "tools/ci/run_host_sanitizers.sh"],
        ["bash", "-n", "tools/qfs/run_crash_tests.sh"],
        ["bash", "-n", "tools/qboot/test_qfs_ide.sh"],
        ["bash", "-n", "tools/qboot/test_bios_boot.sh"],
        ["bash", "-n", "tools/qboot/test_uefi_boot.sh"],
        ["bash", "-n", "tools/qemu_boot_test.sh"],
        ["bash", "-n", "tools/qboot/build_qfs_ide_images.sh"],
        ["make", "test-qbuild"],
    ]
    for command in commands:
        print("[CHECK] " + " ".join(command), flush=True)
        result = subprocess.run(command, cwd=root, check=False)
        if result.returncode != 0:
            raise RuntimeError(f"Validation failed ({result.returncode}): {' '.join(command)}")


def main() -> int:
    global read_original_qbuild
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path.cwd(), help="qmjyOS repository root (default: current directory)")
    parser.add_argument("--check-only", action="store_true", help="validate patchability without changing files")
    args = parser.parse_args()
    root = args.root.expanduser().resolve()

    expected = root / ".git"
    if not expected.exists():
        die(f"Not a Git working tree: {root}")

    original: Dict[str, str] = {rel: read_text(root, rel) for rel in TARGETS}
    verify_makefile(original["Makefile"])
    read_original_qbuild = original["tools/qbuild/qbuild_core.py"]

    updated: Dict[str, str] = {}
    updated["tools/qbuild/qbuild_core.py"], qbuild_changed = patch_qbuild_core(
        original["tools/qbuild/qbuild_core.py"]
    )
    updated["tests/qbuild/test_qbuild_core.py"], tests_changed = patch_qbuild_tests(
        original["tests/qbuild/test_qbuild_core.py"]
    )
    updated["tools/ci/run_host_sanitizers.sh"], sanitizer_changed = patch_host_sanitizers(
        original["tools/ci/run_host_sanitizers.sh"]
    )
    updated["tools/qfs/run_crash_tests.sh"], qfs_crash_changed = patch_qfs_crash_log(
        original["tools/qfs/run_crash_tests.sh"]
    )
    updated["tools/qboot/test_qfs_ide.sh"], qfs_ide_changed = patch_qfs_ide_id_validation(
        original["tools/qboot/test_qfs_ide.sh"]
    )
    updated[".github/workflows/c-cpp.yml"], workflow_changed = patch_workflow(
        original[".github/workflows/c-cpp.yml"]
    )

    changed = {
        rel: updated[rel]
        for rel in updated
        if updated[rel] != original[rel]
    }
    print("[PLAN] Files to update: " + (", ".join(changed) if changed else "none (already repaired)"))
    print("[PLAN] BIOS test-boot and UEFI test-qboot-uefi are selected separately.")
    print("[PLAN] BIOS/QFS IDE runtime checks remain in test-boot; UEFI OVMF remains in test-qboot-uefi.")
    print("[PLAN] CI artifacts retain QBuild reports, BIOS/UEFI serial logs, QEMU logs and QFS IDE runtime evidence.")
    if args.check_only:
        print("[CHECK ONLY PASS] Current files are patchable; no file was changed.")
        return 0
    if not changed:
        run_validation(root)
        print("[ALREADY REPAIRED] QBuild regression tests pass.")
        return 0

    stamp = _dt.datetime.now().strftime("%Y%m%d-%H%M%S-%f")
    backup_dir = root / "build" / "ci-repair-backups" / stamp
    stage_dir = root / "build" / "ci-repair-stage" / stamp
    backup_dir.mkdir(parents=True, exist_ok=False)
    stage_dir.mkdir(parents=True, exist_ok=False)

    written: List[str] = []
    try:
        for rel, content in changed.items():
            src = root / rel
            backup = backup_dir / rel
            backup.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, backup)
            staged = stage_dir / rel
            staged.parent.mkdir(parents=True, exist_ok=True)
            staged.write_text(content, encoding="utf-8", newline="\n")
            os.chmod(staged, src.stat().st_mode)

        # All transformations and backups are ready; now atomically replace.
        for rel in changed:
            staged = stage_dir / rel
            target = root / rel
            os.replace(staged, target)
            written.append(rel)

        run_validation(root)
    except Exception as exc:
        print(f"[ROLLBACK] {exc}", file=sys.stderr)
        for rel in reversed(written):
            backup = backup_dir / rel
            target = root / rel
            if backup.is_file():
                shutil.copy2(backup, target)
        print(f"[ROLLBACK COMPLETE] Original files restored from {backup_dir.relative_to(root)}", file=sys.stderr)
        raise SystemExit(1)
    finally:
        shutil.rmtree(stage_dir, ignore_errors=True)
        try:
            stage_dir.parent.rmdir()
        except OSError:
            pass

    report = {
        "status": "passed",
        "applied_at": _dt.datetime.now(_dt.timezone.utc).isoformat(),
        "modified_files": written,
        "backup_dir": str(backup_dir.relative_to(root)),
        "validation": [
            "python3 -m py_compile",
            "bash -n: sanitizer, QFS crash, QFS IDE, BIOS, UEFI, direct QEMU and QFS image builder scripts",
            "make test-qbuild",
        ],
        "git_commit_created": False,
        "git_push_performed": False,
    }
    report_path = root / "build" / "ci-repair-reports" / f"ci-repair-{stamp}.json"
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print("[QMJYOS CI REPAIR PASS] BIOS/UEFI CI separation and artifact retention applied.")
    print("[MODIFIED] " + ", ".join(written))
    print(f"[BACKUPS] {backup_dir.relative_to(root)}")
    print(f"[REPORT] {report_path.relative_to(root)}")
    print("[GIT] No commit or push was performed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
