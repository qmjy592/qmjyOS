# QBuild 1.0.0 — Release Notes / 发布说明

**Version:** 1.0.0
**Release state:** local validation candidate; do not call it publicly released until the project owner completes target-device validation and GitHub Actions verification.

## 中文

QBuild 1.0.0 将原有 QBuild v0.5.0 模块规划器和 Makefile 入口升级为统一的构建与工程管理前端，同时保留原有常用命令。

### 新增能力

- 模块配置安全管理：`module show|enable|disable`；依赖未满足时拒绝启用；支持显式 `--with-deps`；禁用被依赖模块时拒绝操作，支持显式 `--cascade`。
- 配置快照：保存 `modules.conf`、`modules.dep`、`modules.status`，包含 SHA-256 校验清单。
- 配置恢复：校验快照内容与依赖结构，自动创建恢复前快照，恢复失败时回滚。
- 构建/测试报告：实时输出命令日志，同时保存机器可读 JSON，记录 Git SHA、工作区状态、目标、命令、耗时和退出码。
- 环境诊断：检查 Python、Make、Clang、LLD、llvm-objcopy 和模块配置；QEMU/Git 默认作为可选工具。
- 自身回归测试：覆盖依赖与循环检查、模块启停规则、快照完整性、报告 JSON 和 CLI 参数兼容。
- `qbuild test` 先运行 QBuild 自身测试，再运行原有 Makefile 测试目标。

### 兼容与限制

- 原有 `info`、`config`、`modules`、`deps`、`plan`、`build`、`test`、`clean` 命令保留。
- Makefile 仍然负责真实的编译、链接和内核测试。
- 当前只支持 x86_64 + BIOS；UEFI/AArch64 仍明确拒绝。
- 不包含动态内核模块加载器，不代表 QTest 框架、整个 OS 或任何内核子系统已达到生产级成熟度。
- QBuild 版本与 qmjyOS 系统版本独立。

### 本地验收命令

```sh
./qbuild --version
./qbuild doctor
./qbuild self-test
./qbuild config
./qbuild build --arch x86_64 --boot bios --jobs 2
./qbuild test --arch x86_64 --boot bios --jobs 2
./qbuild config snapshot release-checkpoint
./qbuild config snapshots
./qbuild report list
git diff --check
```

正式发布前，还必须在项目实际 Termux 环境验证模块启用/禁用、`--with-deps`、`--cascade`、快照恢复/完整性拒绝，以及新提交的 GitHub Actions 全部通过。

## English

QBuild 1.0.0 upgrades the v0.5.0 module planner and Makefile frontend into a unified build and project-management CLI while preserving the established commands.

### New capabilities

- Safe module enable/disable operations with dependency checks and explicit `--with-deps` / `--cascade` behavior.
- Configuration snapshots for all three module metadata files, protected by SHA-256 manifests.
- Verified restore with an automatic pre-restore safety snapshot and rollback on failure.
- Live build/test output plus JSON reports containing command, target, Git SHA, dirty state, duration and exit code.
- Toolchain and project diagnostics.
- A standard-library regression suite, automatically run before the existing Makefile test targets.

### Compatibility and limitations

- Existing core commands remain available.
- Makefile remains the actual compiler, linker and kernel-test execution layer.
- Only x86_64 + BIOS is supported; UEFI and AArch64 remain unsupported.
- No dynamic kernel module loader or completed QTest runtime is claimed.
- QBuild's version is independent from the qmjyOS operating-system version.

The code is a local validation candidate until target-Termux checks and the new GitHub Actions run pass. A local test pass must not be represented as a remote CI pass.
