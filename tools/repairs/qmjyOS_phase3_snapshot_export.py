#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Export the CURRENT local qmjyOS worktree for safe Phase 3 integration review.

Run from anywhere:
    python3 qmjyOS_phase3_snapshot_export.py --root ~/qmjyOS

Creates (inside the project, never /tmp):
    build/repairs/qmjyOS_phase3_local_snapshot.tar.gz

The archive contains the source tree (tracked + non-ignored untracked source
files), Git HEAD/branch/status, a binary-capable patch against HEAD, and the
latest Phase 3 consolidated JSON report when present. It excludes .git,
build outputs, and common compiler/binary artifacts. It does not modify the
worktree, create commits, push, or change branches.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import os
from pathlib import Path, PurePosixPath
import subprocess
import tarfile
from datetime import datetime, timezone

EXCLUDED_DIRS = {".git", "build", ".venv", "venv", "node_modules", "__pycache__"}
EXCLUDED_SUFFIXES = {
    ".o", ".d", ".elf", ".bin", ".iso", ".img", ".pyc", ".log",
    ".a", ".so", ".out", ".gcda", ".gcno"
}


def git(root: Path, *args: str, binary: bool = False) -> bytes:
    p = subprocess.run(
        ["git", *args], cwd=root, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, check=False
    )
    if p.returncode != 0:
        message = p.stderr.decode("utf-8", "replace")
        raise RuntimeError(f"git {' '.join(args)} failed ({p.returncode}): {message}")
    return p.stdout if binary else p.stdout


def keep_path(rel: str) -> bool:
    p = PurePosixPath(rel)
    if not rel or p.is_absolute() or ".." in p.parts:
        return False
    if any(part in EXCLUDED_DIRS for part in p.parts):
        return False
    if p.suffix.lower() in EXCLUDED_SUFFIXES:
        return False
    return True


def add_bytes(tf: tarfile.TarFile, name: str, data: bytes) -> None:
    info = tarfile.TarInfo(name)
    info.size = len(data)
    info.mtime = int(datetime.now(timezone.utc).timestamp())
    info.mode = 0o644
    tf.addfile(info, io.BytesIO(data))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--root", default="~/qmjyOS", help="local repository root")
    args = ap.parse_args()
    root = Path(args.root).expanduser().resolve()
    if not (root / ".git").exists():
        raise SystemExit(f"[ABORT] not a Git worktree: {root}")

    head = git(root, "rev-parse", "HEAD").decode().strip()
    branch = git(root, "branch", "--show-current").decode().strip() or "(detached HEAD)"
    status = git(root, "status", "--short", "--branch").decode("utf-8", "replace")
    tracked_and_untracked = git(root, "ls-files", "-co", "--exclude-standard", "-z").decode("utf-8", "surrogateescape").split("\0")
    paths = sorted({p for p in tracked_and_untracked if keep_path(p) and (root / p).is_file()})
    if not paths:
        raise SystemExit("[ABORT] no source files found in worktree")

    # `git diff HEAD` includes both staged and unstaged tracked modifications.
    patch = git(root, "diff", "--binary", "HEAD", "--",
                ".", ":(exclude)build/**", ":(exclude)**/*.o",
                ":(exclude)**/*.elf", ":(exclude)**/*.bin")
    untracked = git(root, "ls-files", "--others", "--exclude-standard", "-z").decode("utf-8", "surrogateescape").split("\0")
    untracked_list = sorted(p for p in untracked if keep_path(p) and (root / p).is_file())

    out_dir = root / "build" / "repairs"
    out_dir.mkdir(parents=True, exist_ok=True)
    output = out_dir / "qmjyOS_phase3_local_snapshot.tar.gz"

    report_candidates = sorted(
        (out_dir).glob("phase3-consolidated-report-*.json"),
        key=lambda p: p.stat().st_mtime,
        reverse=True,
    )
    report_bytes = report_candidates[0].read_bytes() if report_candidates else b""

    manifest = (
        "qmjyOS Phase 3 local source snapshot\n"
        f"created_utc: {datetime.now(timezone.utc).isoformat()}\n"
        f"repository_root: {root}\n"
        f"git_head: {head}\n"
        f"git_branch: {branch}\n\n"
        "git_status:\n" + status + "\n"
        f"source_file_count: {len(paths)}\n"
        f"untracked_source_file_count: {len(untracked_list)}\n"
        f"latest_phase3_report: {report_candidates[0].name if report_candidates else '(not found)'}\n"
        "\nNotes: .git metadata and build outputs are excluded. The patch captures\n"
        "staged/unstaged tracked changes against HEAD; untracked source files are\n"
        "included as normal files in source/.\n"
    ).encode("utf-8")

    with tarfile.open(output, "w:gz", compresslevel=6) as tf:
        add_bytes(tf, "handoff/manifest.txt", manifest)
        add_bytes(tf, "handoff/worktree.patch", patch)
        for rel in paths:
            try:
                data = (root / rel).read_bytes()
            except OSError as exc:
                raise RuntimeError(f"cannot read {rel}: {exc}") from exc
            add_bytes(tf, f"source/{rel}", data)
        if report_bytes:
            add_bytes(tf, f"handoff/{report_candidates[0].name}", report_bytes)

    digest = hashlib.sha256(output.read_bytes()).hexdigest()
    print(f"[OK] Snapshot created: {output}")
    print(f"[INFO] HEAD: {head}")
    print(f"[INFO] Branch: {branch}")
    print(f"[INFO] Source files: {len(paths)} (untracked: {len(untracked_list)})")
    print(f"[INFO] Size: {output.stat().st_size} bytes")
    print(f"[SHA256] {digest}")
    print("[SAFE] No source files changed; no commit or push was performed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
