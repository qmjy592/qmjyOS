#!/usr/bin/env python3
"""Regression tests for QBuild 1.0.0 management primitives."""
from __future__ import annotations
import json
import os
import sys
import tempfile
import unittest
from unittest.mock import patch
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/qbuild"))
import qbuild_core as q


class QBuildCoreTests(unittest.TestCase):
    def setUp(self):
        (ROOT / "build/qbuild").mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=ROOT / "build/qbuild")
        self.root = Path(self.temp.name)
        (self.root / "config").mkdir()
        (self.root / "tools/qbuild").mkdir(parents=True)
        (self.root / "build/qbuild").mkdir(parents=True)
        (self.root / "config/modules.conf").write_text(
            "# config\nBASE=enabled\nMID=disabled\nTOP=disabled\nUNUSED=optional\n", encoding="utf-8"
        )
        (self.root / "config/modules.dep").write_text(
            "BASE:\nMID: BASE\nTOP: MID\nUNUSED:\n", encoding="utf-8"
        )
        (self.root / "config/modules.status").write_text(
            "BASE=implemented\nMID=experimental\nTOP=planned\nUNUSED=planned\n", encoding="utf-8"
        )
        (self.root / "tools/qbuild/module_plan.py").write_text(
            "import sys\nprint('planner PASS')\nsys.exit(0)\n", encoding="utf-8"
        )

    def tearDown(self):
        self.temp.cleanup()

    def test_configuration_validates(self):
        cfg, deps, status = q.load_configuration(self.root)
        self.assertEqual(cfg["BASE"], "enabled")
        self.assertEqual(q.dependency_closure("TOP", deps), {"BASE", "MID", "TOP"})

    def test_duplicate_module_rejected(self):
        path = self.root / "config/modules.conf"
        path.write_text("BASE=enabled\nBASE=disabled\n", encoding="utf-8")
        with self.assertRaises(ValueError):
            q.read_key_values(path, {"enabled", "disabled", "optional"})

    def test_cycle_rejected(self):
        with self.assertRaisesRegex(ValueError, "循环依赖"):
            q.validate_configuration(
                {"A": "enabled", "B": "enabled"},
                {"A": ["B"], "B": ["A"]},
                {"A": "planned", "B": "planned"},
            )

    def test_enable_requires_deps_unless_requested(self):
        with self.assertRaisesRegex(ValueError, "--with-deps"):
            q.command_enable(self.root, "TOP", False)
        q.command_enable(self.root, "TOP", True)
        cfg, _, _ = q.load_configuration(self.root)
        self.assertEqual(cfg["TOP"], "enabled")
        self.assertEqual(cfg["MID"], "enabled")
        self.assertEqual(cfg["BASE"], "enabled")

    def test_disable_blocks_dependents_and_cascade_works(self):
        q.command_enable(self.root, "TOP", True)
        with self.assertRaisesRegex(ValueError, "仍有启用模块依赖它"):
            q.command_disable(self.root, "MID", False)
        q.command_disable(self.root, "MID", True)
        cfg, _, _ = q.load_configuration(self.root)
        self.assertEqual(cfg["MID"], "disabled")
        self.assertEqual(cfg["TOP"], "disabled")
        self.assertEqual(cfg["BASE"], "enabled")

    def test_snapshot_restore_and_automatic_safety_snapshot(self):
        snap = q.create_snapshot(self.root, "baseline")
        q.command_enable(self.root, "TOP", True)
        safety = q.restore_snapshot(self.root, "baseline")
        cfg, _, _ = q.load_configuration(self.root)
        self.assertEqual(cfg["TOP"], "disabled")
        self.assertTrue(safety.is_dir())
        self.assertEqual(len(q.list_snapshots(self.root)), 2)

    def test_tampered_snapshot_is_rejected_without_mutation(self):
        snap = q.create_snapshot(self.root, "tamper-check")
        (snap / "modules.conf").write_text("BROKEN=enabled\n", encoding="utf-8")
        before = (self.root / "config/modules.conf").read_text(encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "完整性校验失败"):
            q.restore_snapshot(self.root, "tamper-check")
        self.assertEqual((self.root / "config/modules.conf").read_text(encoding="utf-8"), before)

    def test_module_change_rolls_back_when_full_planner_fails(self):
        config_path = self.root / "config/modules.conf"
        before = config_path.read_text(encoding="utf-8")
        (self.root / "tools/qbuild/module_plan.py").write_text(
            "import sys\nprint('planner FAIL')\nsys.exit(7)\n", encoding="utf-8"
        )
        with self.assertRaisesRegex(ValueError, "完整性检查失败"):
            q.command_enable(self.root, "TOP", True)
        self.assertEqual(config_path.read_text(encoding="utf-8"), before)

    def test_restore_can_recover_invalid_working_config(self):
        q.create_snapshot(self.root, "known-good")
        config_path = self.root / "config/modules.conf"
        config_path.write_text("BROKEN LINE\n", encoding="utf-8")
        q.restore_snapshot(self.root, "known-good")
        cfg, _, _ = q.load_configuration(self.root)
        self.assertEqual(cfg["BASE"], "enabled")

    def test_failed_restore_rolls_back_to_pre_restore_state(self):
        q.create_snapshot(self.root, "known-good")
        config_path = self.root / "config/modules.conf"
        config_path.write_text(
            "# config\nBASE=enabled\nMID=enabled\nTOP=enabled\nUNUSED=optional\n",
            encoding="utf-8",
        )
        before = config_path.read_text(encoding="utf-8")
        (self.root / "tools/qbuild/module_plan.py").write_text(
            "import sys\nprint('planner FAIL')\nsys.exit(9)\n", encoding="utf-8"
        )
        with self.assertRaisesRegex(ValueError, "恢复后的 QBuild 完整性检查失败"):
            q.restore_snapshot(self.root, "known-good")
        self.assertEqual(config_path.read_text(encoding="utf-8"), before)

    def test_report_output_is_machine_readable(self):
        report = {
            "schema": 1, "qbuild_version": q.VERSION, "kind": "test",
            "status": "passed", "return_code": 0,
        }
        path = self.root / "build/qbuild/report.json"
        q.write_json_atomic(path, report)
        self.assertEqual(json.loads(path.read_text(encoding="utf-8"))["qbuild_version"], "1.0.0")

    def test_target_matrix_accepts_x86_64_uefi(self):
        q.validate_target("x86_64", "uefi", 2)
        with self.assertRaisesRegex(RuntimeError, "目标尚未实现"):
            q.validate_target("aarch64", "uefi", 2)

    def test_cli_compatibility(self):
        args = q.build_parser().parse_args(["test", "--arch", "x86_64", "--boot", "bios", "--jobs", "2"])
        self.assertEqual(args.command, "test")
        self.assertEqual(args.jobs, 2)
        args = q.build_parser().parse_args(["build", "--module", "FS_VFS", "--module", "FS_QFS"])
        self.assertEqual(args.module, ["FS_VFS", "FS_QFS"])

    def test_tracked_command_writes_log_and_result_report(self):
        code = q.run_tracked(
            self.root, "selftest", [sys.executable, "-c", "print('tracked-output')"],
            {"arch": "x86_64", "boot": "bios"},
        )
        self.assertEqual(code, 0)
        reports = list((self.root / "build/qbuild/reports").glob("*.json"))
        logs = list((self.root / "build/qbuild/reports").glob("*.log"))
        self.assertEqual(len(reports), 1)
        self.assertEqual(len(logs), 1)
        report = json.loads(reports[0].read_text(encoding="utf-8"))
        self.assertEqual(report["status"], "passed")
        self.assertEqual(report["return_code"], 0)
        self.assertIn("tracked-output", logs[0].read_text(encoding="utf-8"))

    def test_tracked_command_records_failure(self):
        code = q.run_tracked(
            self.root, "failed-test", [sys.executable, "-c", "raise SystemExit(6)"], {},
        )
        self.assertEqual(code, 6)
        reports = list((self.root / "build/qbuild/reports").glob("*.json"))
        self.assertEqual(json.loads(reports[0].read_text(encoding="utf-8"))["status"], "failed")

    def test_snapshot_path_traversal_rejected(self):
        with self.assertRaises(ValueError):
            q.create_snapshot(self.root, "../escape")

    def test_firmware_specific_test_selection(self):
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

if __name__ == "__main__":
    unittest.main(verbosity=2)
