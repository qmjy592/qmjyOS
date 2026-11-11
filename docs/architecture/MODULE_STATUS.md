# qmjyOS 模块状态清单

版本：0.1.0
阶段：第一部分——系统骨架建设

## 状态定义

| 状态 | 含义 |
|---|---|
| PLANNED | 已规划，尚未实现 |
| INTERFACE | 已建立接口，功能尚未完整实现 |
| IMPLEMENTED | 已有实现，尚未充分验证 |
| BUILD_VERIFIED | 构建验证通过 |
| RUNTIME_VERIFIED | 实际运行验证通过 |

注意：
- 目录存在不代表模块已实现。
- 头文件存在不代表功能已实现。
- 编译成功不代表运行成功。
- 测试通过只代表对应测试覆盖的功能通过。

## 模块状态

| 模块 | 路径 | 当前状态 | 说明 |
|---|---|---|---|
| 公共基础类型 | include/qmjy | BUILD_VERIFIED | 六个公共头文件语法检查通过 |
| 内核核心 | kernel/core | RUNTIME_VERIFIED | x86_64 启动路径已验证 |
| 物理内存管理 | mm/physical | RUNTIME_VERIFIED | 分配器及自测试通过 |
| 分页管理 | mm/paging | BUILD_VERIFIED | 构建及已有自测试通过；完整硬件场景仍需验证 |
| GDT/TSS | arch/x86_64/gdt | RUNTIME_VERIFIED | 初始化运行通过 |
| IDT/中断 | arch/x86_64/idt | IMPLEMENTED | 需要进一步验证完整异常和中断场景 |
| x86_64 启动入口 | arch/x86_64/boot | RUNTIME_VERIFIED | 当前 BIOS 启动路径已运行 |
| BIOS 启动代码 | boot/bios | RUNTIME_VERIFIED | 当前引导链已验证，不代表 QBoot 已完成 |
| QBoot | boot/qboot | PLANNED | 自研 BIOS/UEFI 引导管理器尚待实现 |
| ARM64 架构 | arch/aarch64 | PLANNED | 已有目录，尚未验证完整构建和启动 |
| 任务调度 | sched | IMPLEMENTED | 当前已验证协作式调度 |
| 进程管理 | kernel/process | PLANNED | 完整进程管理尚待实现 |
| 线程管理 | kernel/thread | PLANNED | 完整线程管理尚待实现 |
| IPC | kernel/ipc | PLANNED | 尚待实现 |
| 系统调用 | kernel/syscall | INTERFACE | 已有基础 Ring 3 系统调用测试路径，完整 ABI 尚未建立 |
| 串口驱动 | drivers/serial | RUNTIME_VERIFIED | 当前启动日志输出正常 |
| PCI 驱动框架 | drivers/pci | PLANNED | 尚待实现 |
| USB 驱动框架 | drivers/usb | PLANNED | 尚待实现 |
| 网络驱动 | drivers/network | PLANNED | 尚待实现 |
| 显示驱动 | drivers/display | PLANNED | 尚待实现 |
| 音频驱动 | drivers/audio | PLANNED | 尚待实现 |
| 蓝牙驱动 | drivers/bluetooth | PLANNED | 尚待实现 |
| VFS | fs/vfs | RUNTIME_VERIFIED | 主机测试通过，内核启动集成通过 |
| RAMFS | fs/ramfs | RUNTIME_VERIFIED | 主机测试通过，内核挂载通过 |
| Btrfs | fs/btrfs | PLANNED | 尚未实现 |
| FAT 文件系统 | fs | PLANNED | 尚待实现 |
| DevFS | fs/devfs | PLANNED | 尚待实现 |
| ProcFS | fs/procfs | PLANNED | 尚待实现 |
| SysFS | fs/sysfs | PLANNED | 尚待实现 |
| 存储管理 | storage | PLANNED | 尚待实现 |
| 网络协议栈 | network | PLANNED | 尚待实现 |
| 用户态运行时 | runtime | PLANNED | 尚待实现 |
| 系统服务 | services | PLANNED | 尚待实现 |
| QShell | shell | PLANNED | 尚待实现 |
| 桌面系统 | desktop | PLANNED | 尚待实现 |
| 应用程序 | apps | PLANNED | 尚待实现 |
| 软件包管理 | package | PLANNED | 尚待实现 |
| 安装器 | installer | PLANNED | 尚待实现 |
| 恢复环境 | recovery | PLANNED | 尚待实现 |
| QBuild | tools/qbuild | IMPLEMENTED | v0.1 已具备基础构建功能，完整多架构能力尚待实现 |
| QDebug | tools/qdebug | PLANNED | 尚待实现 |
| QTest | tools/qtest | PLANNED | 尚待实现 |

## 模块完成验收条件

一个模块只有同时满足以下条件，才能认定为完整：

1. 职责和边界明确。
2. 公共接口及数据结构明确。
3. 依赖关系明确。
4. 实际实现完整。
5. 已纳入 QBuild。
6. 单元测试通过。
7. 集成测试通过。
8. 对硬件相关模块完成对应架构的运行验证。
9. 文档与实际代码一致。

## 更新规则

每次模块状态变化时，必须更新本文件。

不得将 PLANNED 或 INTERFACE 直接标记为 RUNTIME_VERIFIED。
