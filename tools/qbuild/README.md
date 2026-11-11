# QBuild

qmjyOS 统一构建入口。

当前版本：0.5.0

QBuild 当前负责读取模块配置、检查模块依赖与状态、生成模块构建计划，
并调用项目现有 Makefile 完成构建和测试。

## 当前支持目标

- x86_64 + BIOS

## 尚未实现的目标

- x86_64 + UEFI
- ARM64 启动与构建

## 常用命令

在项目根目录执行：

    ./tools/qbuild/qbuild help
    ./tools/qbuild/qbuild info
    ./tools/qbuild/qbuild config
    ./tools/qbuild/qbuild modules
    ./tools/qbuild/qbuild build --arch x86_64 --boot bios
    ./tools/qbuild/qbuild test
    ./tools/qbuild/qbuild clean

并行构建或测试：

    ./tools/qbuild/qbuild build --arch x86_64 --boot bios --jobs 4
    ./tools/qbuild/qbuild test --jobs 4

也可以通过 QBUILD_JOBS 环境变量设置并行任务数：

    QBUILD_JOBS=4 ./tools/qbuild/qbuild test

## 模块管理

QBuild 使用以下配置文件：

- config/modules.conf：模块配置
- config/modules.dep：模块依赖关系
- config/modules.status：模块实现状态

当前模块状态只允许：

- planned
- experimental
- implemented

QBuild 会检查模块配置、依赖关系、循环依赖、模块状态和源码映射，
并生成模块构建计划。

模块状态表示实现成熟度，不代表运行时验证结果。

## 测试

QBuild 的测试命令会调用项目 Makefile 中的测试目标，包括：

- test-memory
- test-paging
- test-vfs
- test-ramfs
- test-interfaces
- test-boot

公共接口头文件测试也可以单独运行：

    make test-interfaces

该测试使用主机编译器编译公共 qmjyOS 头文件，
只检查头文件兼容性和声明一致性，不验证接口的运行时实现。

## QTest 状态

当前测试入口由 Makefile 测试目标组成，
尚不是完整的 QTest 运行时框架。

QTest 的后续实现计划见：

tests/qtest/README.md

## 兼容性

QBuild 当前仍以项目现有 Makefile 作为实际构建执行基础。
原有 make 命令继续可用。

## 实现边界

当前 QBuild 版本不代表以下功能已经实现：

- 动态内核模块加载
- 完整的交互式配置系统
- 独立于现有 Makefile 的完整模块链接系统
- UEFI 和 ARM64 完整构建链

## QBuild v0.5.0 — Phase 1 verification

QBuild v0.5.0 is the build and module-management baseline for the first
qmjyOS system-foundation phase.

The verified x86_64 + BIOS workflow includes:

- Configuration and module dependency validation
- Dependency cycle detection and topological planning
- Module status inventory validation
- Source-to-object mapping existence and uniqueness checks
- Enabled-module dependency validation
- Full kernel build integration
- Host-side memory, paging, VFS, and RAMFS tests
- Public interface header compilation checks
- Static BIOS boot-image verification

The current module inventory contains 51 modules. Source mappings exist
for 9 modules. Module status metadata reports 1 implemented, 9 experimental,
and 41 planned modules.

These status values are metadata, not a claim that every planned module
has an implementation or that every experimental module is production-ready.

The currently supported build target is x86_64 + BIOS. Other targets must
remain explicitly unsupported until their toolchains, boot paths, and
validation workflows are implemented.

Boot static checks do not replace QEMU or physical-hardware runtime tests.
