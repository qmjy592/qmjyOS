# QBuild

qmjyOS 统一构建入口。

当前版本：0.1.0

## 支持目标

- x86_64 + BIOS

## 尚未实现

- x86_64 + UEFI
- ARM64 启动与构建

## 命令

```sh
./qbuild help
./qbuild info
./qbuild build --arch x86_64 --boot bios
./qbuild test
./qbuild clean
并行构建
默认使用 2 个并行任务。
可以通过参数或环境变量调整：./qbuild build --jobs 4
QBUILD_JOBS=4 ./qbuild test
兼容性
QBuild 0.1.0 暂时调用项目原有 Makefile，不替换原构建规则。
原有 make 命令继续可用。
