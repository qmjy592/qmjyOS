# QBoot Handoff Protocol v1

QBoot 是 qmjyOS 自研启动管理器。

本目录定义 QBoot 与内核之间的统一启动信息交接协议。
协议定义不代表 BIOS、UEFI 或 ARM64 启动器已经实现。

## 1. 基本结构

公共定义：

include/qmjy/boot.h

核心结构：

qm_boot_info_t

启动信息至少包含：

- 协议版本
- 目标架构
- 固件类型
- 内存映射
- 内核物理地址范围
- 内核虚拟入口
- 启动参数
- 已加载模块
- 可选 framebuffer
- ACPI RSDP
- UEFI System Table

## 2. 入口约定

| 平台 | 入口状态 | 启动信息指针 |
|---|---|---|
| x86_64 BIOS | 64 位 Long Mode | RDI |
| x86_64 UEFI | 退出 Boot Services 后 | RDI |
| AArch64 | EL1 | X0 |

具体寄存器状态、分页状态和 CPU 特性必须由协议标志进一步描述。

## 3. 地址约定

结构中的物理地址字段均为物理地址。

内核不得假设未报告的物理内存可用。

内存映射必须满足：

- 地址范围有效
- 不存在重叠
- 长度非零
- 类型明确
- 未知类型默认保留

## 4. 版本兼容

协议主版本改变代表不兼容修改。

新增尾部字段必须增加 struct_size，并提升次版本。

旧内核不得访问 struct_size 范围之外的字段。

v1 保留字段必须为零。

## 5. 生命周期

QBoot 必须保证启动信息及其引用的数据在内核完成复制或明确接管之前保持有效。

## 6. 实现边界

当前 x86_64 BIOS 启动路径仍属于开发用 bootstrap。

以下功能尚未完成：

- 正式 QBoot BIOS 启动器
- QBoot UEFI 应用
- ARM64 启动器
- 启动菜单
- 启动项持久化
- 文件系统驱动
- 启动映像签名验证
- Secure Boot 策略
