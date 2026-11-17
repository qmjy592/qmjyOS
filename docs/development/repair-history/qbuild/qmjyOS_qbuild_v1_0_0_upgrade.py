#!/usr/bin/env python3
"""Transactional local installer for QBuild 1.0.0."""
from __future__ import annotations
import base64, datetime, hashlib, io, json, os, shutil, subprocess, sys, tarfile, time
from pathlib import Path

PAYLOAD_B64 = (
    'H4sIAILE+moC/+19fXsTR5J4/tanmJ1sDikryZYBA97z3hEwG98RYLGT53dnvPJYGtsTJI0yIwE+r58HNiG8hLdNgCRAQkhCwpGN'
    'TTa5xNi8fJc7SzJ/5Sv8qrq6e3pe9GIgXG4jPQ9Yo+mX6u6q6qrqqup0T7rnn/cZR142jbzpPPeTfHrp0+xvb+/Gjd53/D3T25fJ'
    'PKcdee4ZfKpuxXCg++d+mZ++LVqxYhXNwcyWbZu2Zrb2Z/rSm/szWzZuij3X/fz9fyq2XXB73pisWoU8//OT0P+WzZtVus9s2ZyR'
    'NN+3sfe5zOa+Lf2bN2V6+6FcZlN/L9B/77Okf8e2K63K2YaVdWcMx8z/Xa3/87/qyRsVg/7L2cV0xXSK1SM9U1bBdHuqrtMzaZV6'
    '3JmYa1a0lFmNjezYP7xvNLtzeP/gr+M7du7bPvryoJbLa6mUpv86nrecklE06alXT+jaP/yDVj6cT8TMI2ZOK89WZuzSRnjnNcOx'
    'LpuzHTNdntXh5T/rXebzjD7pn8X+vym8//d19/9nsv9v9e3/2zJb+9Jbtm0EMaCvS4O/zP1fcOKnSv/9mzY12f9h29/SL/b/vszm'
    'LUD//X1b+rr7/zPa/8Umb5YOiQ06puv6H15CZNAy6d5074AGszRZMDWQEIpGKZ8qWCVTY9iiwaNWduzXzVwlBe+MabNoliralGOX'
    'KmYpn4amYvBQ1LLZqWql6pjZrGYVy7ZTgaolu2JULLvkxmLiN2e6bDiuKZ5nDHemYE2Kx9dduyS+2674Vi4YlSnbKYpnR1Z3Z6oV'
    'qyCfqpMAas50ZU13Vn5FNkiQgjBk4pOAUzwnWZn/sEu8XNmoIGyi2D54pBeV2bJVmha/D4NMhbMXi702tH9keO8ebVDT2bzqsR17'
    '9+wa/n121/DuoRH4Oa4X7XwVRK90zi5N6UlNPufNsvoI61Gpunoi9srena/uHsru2f7KUHb/EDQBtAurVAb5Le7ofxzbnvr3cfyv'
    'N7UtO/7ir6HGyJ7t+0Ze3jvaso6R+g+oMe59TWdT43O9yS3b5rGNnUO7tr+6ezQ7OjQC/23f//uhUQZ+TIOPXjHdSoqYCcLMHotm'
    '0XZm5WPZmIYZko8zplGWD4emXPndMYr4pLarvLUAxZwpAxZU/nTQdEpmITVlV0t5hlryjVmY6t/ka4rjgizhms4hS21sEpgC1EjE'
    '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'
    '9fP/AZAQ6bgAGAEA'
)
PAYLOAD_PATHS = [
    "tools/qbuild/qbuild",
    "tools/qbuild/qbuild_core.py",
    "tools/qbuild/README.md",
    "tests/qbuild/test_qbuild_core.py",
    "docs/development/QBUILD_V1.0.0_RELEASE_NOTES_CN_EN.md",
]
APPEND_MARKERS = {
    "README.md": "<!-- QBUILD-1.0.0-2026-11-16 -->",
    "BUILD_AND_TESTING_CN_EN.md": "<!-- QBUILD-1.0.0-VALIDATION-2026-11-16 -->",
}

def run(command, root):
    print("[CHECK] " + " ".join(command))
    result = subprocess.run(command, cwd=root, text=True, check=False)
    if result.returncode != 0:
        raise RuntimeError(f"命令失败，退出码 {result.returncode}: {' '.join(command)}")

def main():
    root = Path(__file__).resolve().parent
    if not (root / ".git").exists() or not (root / "config/modules.conf").is_file():
        raise SystemExit("请将本脚本放在 qmjyOS 仓库根目录运行。")
    marker = root / "build/qbuild-v1.0.0.applied"
    if marker.exists():
        raise SystemExit("QBuild v1.0.0 补丁标记已存在；拒绝重复应用。")
    required = [
        "tools/qbuild/qbuild", "tools/qbuild/module_plan.py", "tools/qbuild/README.md",
        "config/modules.conf", "config/modules.dep", "config/modules.status",
        "Makefile", "README.md", "BUILD_AND_TESTING_CN_EN.md",
    ]
    missing = [p for p in required if not (root / p).is_file()]
    if missing:
        raise SystemExit("仓库缺少必需文件：" + ", ".join(missing))
    old_cli = (root / "tools/qbuild/qbuild").read_text(encoding="utf-8")
    old_plan = (root / "tools/qbuild/module_plan.py").read_text(encoding="utf-8")
    makefile = (root / "Makefile").read_text(encoding="utf-8")
    if 'VERSION="0.5.0"' not in old_cli:
        raise SystemExit("QBuild CLI 不是预期的 v0.5.0 基线；为避免覆盖未知改动，已停止。")
    if old_plan.count("v0.5.0") < 1:
        raise SystemExit("module_plan.py 中未找到 v0.5.0 版本锚点；为避免误改，已停止。")
    if "test-boot:" not in makefile:
        raise SystemExit("当前 Makefile 缺少 test-boot 目标，无法确认测试接口兼容性。")
    if "QBUILD-1.0.0-SELFTEST-TARGET" in makefile:
        raise SystemExit("Makefile 已包含 QBuild 1.0.0 自测目标标记；拒绝重复追加。")
    for path, token in APPEND_MARKERS.items():
        if token in (root / path).read_text(encoding="utf-8"):
            raise SystemExit(f"{path} 已包含 QBuild 1.0.0 文档标记；拒绝重复追加。")

    archive = base64.b64decode(PAYLOAD_B64)
    payload = {}
    with tarfile.open(fileobj=io.BytesIO(archive), mode="r:gz") as tf:
        for member in tf.getmembers():
            if not member.isfile():
                continue
            name = member.name
            if name.startswith("/") or ".." in Path(name).parts:
                raise SystemExit("升级包中出现非法路径。")
            stream = tf.extractfile(member)
            if stream is None:
                raise SystemExit("无法读取升级包文件：" + name)
            payload[name] = stream.read()
    for path in PAYLOAD_PATHS + ["README_APPEND.md", "BUILD_APPEND.md", "MAKEFILE_APPEND.md"]:
        if path not in payload:
            raise SystemExit("升级包缺少文件：" + path)

    payload["tools/qbuild/module_plan.py"] = old_plan.replace("v0.5.0", "v1.0.0").encode("utf-8")
    payload["README.md"] = (root / "README.md").read_bytes() + b"\n" + payload["README_APPEND.md"]
    payload["BUILD_AND_TESTING_CN_EN.md"] = (root / "BUILD_AND_TESTING_CN_EN.md").read_bytes() + b"\n" + payload["BUILD_APPEND.md"]
    payload["Makefile"] = makefile.encode("utf-8") + b"\n" + payload["MAKEFILE_APPEND.md"]
    del payload["README_APPEND.md"]
    del payload["BUILD_APPEND.md"]
    del payload["MAKEFILE_APPEND.md"]

    touched = list(dict.fromkeys(PAYLOAD_PATHS + ["Makefile", "tools/qbuild/module_plan.py", "README.md", "BUILD_AND_TESTING_CN_EN.md"]))
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    backup = root / "build/backups" / f"qbuild-v1.0.0-{stamp}-{os.getpid()}"
    stage = root / "build" / f"qbuild-v1.0.0-stage-{os.getpid()}-{time.time_ns()}"
    if backup.exists() or stage.exists():
        raise SystemExit("备份或暂存目录已存在；为避免覆盖，已停止。")
    backup.mkdir(parents=True)
    stage.mkdir(parents=True)
    existed = {}
    applied = []
    try:
        for rel in touched:
            target = root / rel
            if target.exists():
                existed[rel] = True
                saved = backup / rel
                saved.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(target, saved)
            else:
                existed[rel] = False
        for rel in touched:
            staged = stage / rel
            staged.parent.mkdir(parents=True, exist_ok=True)
            staged.write_bytes(payload[rel])
        for rel in touched:
            target = root / rel
            target.parent.mkdir(parents=True, exist_ok=True)
            os.replace(stage / rel, target)
            applied.append(rel)
        os.chmod(root / "tools/qbuild/qbuild", 0o755)

        run([sys.executable, "-m", "py_compile", "tools/qbuild/qbuild_core.py", "tests/qbuild/test_qbuild_core.py", "tools/qbuild/module_plan.py"], root)
        run(["sh", "-n", "tools/qbuild/qbuild"], root)
        run([sys.executable, "tools/qbuild/qbuild_core.py", "--version"], root)
        run(["make", "test-qbuild"], root)
        run([sys.executable, "tools/qbuild/module_plan.py", "--check"], root)
        run(["git", "diff", "--check"], root)

        marker.parent.mkdir(parents=True, exist_ok=True)
        marker.write_text(json.dumps({
            "version": "1.0.0", "applied_at": datetime.datetime.now(datetime.timezone.utc).isoformat(),
            "backup": str(backup.relative_to(root)), "files": touched,
            "payload_sha256": hashlib.sha256(archive).hexdigest(),
        }, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    except Exception as error:
        print("[ROLLBACK] 验证失败，正在恢复本次修改。", file=sys.stderr)
        for rel in reversed(applied):
            target = root / rel
            saved = backup / rel
            if existed.get(rel) and saved.is_file():
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(saved, target)
            elif target.exists():
                target.unlink()
        shutil.rmtree(stage, ignore_errors=True)
        try:
            marker.unlink()
        except FileNotFoundError:
            pass
        raise SystemExit(f"QBuild v1.0.0 未安装：{error}；备份保留在 {backup.relative_to(root)}")
    finally:
        shutil.rmtree(stage, ignore_errors=True)

    print("\n[QBUILD 1.0.0] 本地升级补丁应用成功。")
    print("已更新文件：" + str(len(touched)))
    print("备份目录：" + str(backup.relative_to(root)))
    print("安装标记：build/qbuild-v1.0.0.applied")
    print("未执行 git commit 或 push。")
    print("\n下一步：")
    print("  ./qbuild doctor")
    print("  ./qbuild self-test")
    print("  ./qbuild build --arch x86_64 --boot bios --jobs 2")
    print("  ./qbuild test --arch x86_64 --boot bios --jobs 2")
    print("  ./qbuild report list")
    print("  git diff --check")

if __name__ == "__main__":
    main()
