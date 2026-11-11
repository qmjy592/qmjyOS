# Common Architecture Abstraction

本目录定义 qmjyOS 架构无关层与处理器架构实现之间的边界。

## 1. 设计原则

通用内核不得直接依赖某一架构的：

- CPU 特殊寄存器
- 中断控制器寄存器
- 页表硬件格式
- 上下文切换汇编
- CPU 特定启动寄存器
- 架构特定异常帧

通用代码通过 include/qmjy/arch.h 中的公共接口访问架构能力。

## 2. 架构实现目录

规划中的架构目录：

- arch/x86_64/
- arch/aarch64/
- arch/common/

每种架构负责提供自己的实现，不得将 x86_64 专有结构直接暴露给通用内核。

## 3. 架构操作表

qm_arch_ops_t 是架构能力的统一注册接口。

架构实现需要逐步提供：

- early_init
- late_init
- cpu_id
- irq_save
- irq_restore
- halt
- relax
- monotonic_time_ns
- tlb_flush_all
- address_space_activate

具体实现必须检查调用条件、CPU 状态和参数有效性。

## 4. x86_64 规划

职责包括：

- GDT、TSS
- IDT、异常与中断入口
- APIC 与中断控制
- CR0、CR2、CR3、CR4 管理
- x86_64 页表
- 上下文切换
- CPU 特性探测
- BIOS/UEFI 启动交接

## 5. AArch64 规划

职责包括：

- Exception Level 管理
- 异常向量表
- GIC 中断控制
- MMU 与页表
- 系统寄存器管理
- 上下文切换
- CPU 特性探测
- ARM64 启动交接

## 6. 当前实现状态

此文档描述目标边界，不代表两个架构均已完成。

当前实际可构建目标以 QBuild 配置和模块状态清单为准。
