# QBuild 1.0.0

qmjyOS 统一构建与工程管理入口。QBuild 1.0.0 在保留 v0.5.0 命令和 Makefile 执行基础的同时，增加安全模块配置管理、可校验配置快照、可追溯构建/测试报告、环境诊断和自身回归测试。

QBuild 版本与 qmjyOS 操作系统版本独立。QBuild 1.0.0 不代表 qmjyOS 已发布 1.0.0；当前内核支持目标仍是 x86_64 + BIOS。

## 要求与边界

- Python 3.10+、Make、Clang、LLD 和 llvm-objcopy。
- 当前支持：`x86_64 + bios`。
- `x86_64 + uefi` 与 `aarch64` 仍明确拒绝。
- Makefile 继续执行实际编译、链接和原有测试目标。
- 不提供动态内核模块加载器，也不宣称 QTest 运行时已完成。
- 模块成熟度仍由 `config/modules.status` 的 `planned`、`experimental`、`implemented` 表示；QBuild 不会因构建成功自动提升模块状态。

## 常用命令

```sh
./qbuild --version
./qbuild help
./qbuild info
./qbuild doctor
./qbuild config
./qbuild modules
./qbuild deps
./qbuild plan
./qbuild build --arch x86_64 --boot bios --jobs 2
./qbuild build --module FS_VFS --arch x86_64 --boot bios
./qbuild test --arch x86_64 --boot bios --jobs 2
./qbuild self-test
./qbuild report list
./qbuild clean
```

旧命令 `info`、`config`、`modules`、`deps`、`plan`、`build`、`test` 和 `clean` 保留。根目录 `./qbuild` 与 `./tools/qbuild/qbuild` 使用同一入口。

## 模块配置管理

```sh
./qbuild module show FS_VFS
./qbuild module enable SOME_MODULE --with-deps
./qbuild module disable SOME_MODULE
./qbuild module disable SOME_MODULE --cascade
```

启用模块默认要求其依赖已启用；`--with-deps` 才会递归启用依赖。禁用一个仍被启用模块依赖的模块会被拒绝；`--cascade` 才会连同启用的依赖方一起禁用。每次变更都会先检查配置、依赖图、循环依赖和源码映射，失败则恢复原文件。模块状态文件不会被自动改写。

## 配置快照与恢复

```sh
./qbuild config snapshot before-storage-work
./qbuild config snapshots
./qbuild config restore before-storage-work
```

快照保存在 `build/qbuild/snapshots/`（已被 `/build/` 忽略）。每份快照包含 `modules.conf`、`modules.dep`、`modules.status`、SHA-256 清单和创建时间。恢复前会校验清单与配置结构，并自动保存一份恢复前快照；恢复后完整模块检查失败则回滚。

## 构建/测试报告

`build` 与 `test` 会实时显示子进程输出，同时在 `build/qbuild/reports/` 保存 UTF-8 日志和 JSON 报告。报告记录 QBuild 版本、命令、目标架构、并行度、开始/结束时间、耗时、退出码、Git commit SHA 和工作区是否有未提交变更。

```sh
./qbuild report list
./qbuild report show REPORT_ID
```

报告是本地构建证据，不会自动上传，也不替代 GitHub Actions 结果。失败、被中断和通过会分别记录。

## 环境诊断与自测

```sh
./qbuild doctor
./qbuild self-test
```

`doctor` 检查必需工具链和 QBuild 模块配置；QEMU 与 Git 属于可选检查项（`doctor --strict` 会要求可选工具也存在）。`self-test` 使用 Python 标准库测试配置校验、重复项、循环依赖、模块启停依赖约束、级联禁用、快照完整性/恢复、报告 JSON 和旧命令参数兼容性。

`qbuild test` 会调用新增的 `test-qbuild` Makefile 目标先运行 QBuild 自身回归测试，再执行原有内核测试目标。Makefile 仍是内核构建和现有 C/汇编测试的执行基础。

## 报告与数据安全

快照、报告、日志及测试临时目录都位于 `build/` 下，不进入 Git 提交。配置变更只编辑 `config/modules.conf`，不会自动更改依赖定义或成熟度状态。恢复操作会生成恢复前安全快照。请在提交前检查 `git status --short` 与 `git diff --check`。

## 1.0.0 发布门槛

QBuild 代码版本设为 1.0.0；正式对外发布仍须在目标 Termux 环境完成 `doctor`、`self-test`、完整 `build`、完整 `test`、快照/恢复操作验收、`git diff --check`，并确认新提交的 GitHub Actions CI 全部通过。不能把本地 PASS 写成 GitHub CI PASS。
