# qmjyOS System Architecture
# qmjyOS 操作系统总体架构规范

Version: 1.0
Status: Architecture Baseline
Project: qmjyOS

---

## 1. 项目目标

qmjyOS 是面向 x86_64 和 ARM64 的自主操作系统。

系统采用分层、模块化架构。各模块必须具有明确职责、公开接口、
依赖关系、构建入口和测试入口。

不允许仅以目录或 .gitkeep 作为模块完成标志。

## 2. 总体架构分层

系统按以下层次组织：

1. Boot Layer：启动管理与启动协议
2. Architecture Layer：处理器架构相关代码
3. Kernel Core：内核核心与初始化
4. Memory Layer：物理内存、虚拟内存、堆管理
5. Process Layer：进程、线程、调度与同步
6. IPC Layer：进程间通信
7. Driver Layer：设备驱动与设备模型
8. Storage Layer：块设备、分区、文件系统
9. Network Layer：网络设备、协议栈与网络服务
10. System Call Layer：用户态与内核态接口
11. Runtime Layer：程序运行时与基础库
12. System Service Layer：系统服务
13. User Interface Layer：Shell、桌面与窗口系统
14. Application Layer：系统应用
15. Package Layer：软件包、仓库与更新
16. Installer Layer：安装、分区与系统部署
17. Recovery Layer：恢复、修复与系统维护
18. Development Layer：构建、调试、诊断与测试工具

## 3. 模块职责

### 3.1 boot/

负责系统启动，不负责通用内核功能。

- QBoot：qmjyOS 自研启动管理器
- bios：BIOS 启动路径
- uefi：UEFI 启动路径
- common：跨启动固件共用的启动定义
- loader：内核映像加载
- config：启动配置与启动项

约束：
- 不使用 GRUB 作为 qmjyOS 的启动管理器。
- 启动管理器与内核之间必须有明确的启动协议。
- 必须支持启动参数、内存映射和启动设备信息传递。

### 3.2 arch/

负责处理器架构相关实现。

- x86_64：x86_64 架构实现
- arm64：ARM64 架构实现
- common：架构无关的公共定义

架构相关代码包括：
- CPU 初始化
- GDT / IDT 或对应异常机制
- 中断控制器
- 上下文切换
- 原子操作
- CPU 特性检测
- SMP 启动
- 架构相关系统调用入口

通用内核不得直接依赖某个架构的私有实现。

### 3.3 kernel/

负责内核核心。

- core：内核入口与初始化流程
- init：启动阶段初始化管理
- panic：内核致命错误处理
- log：内核日志
- printk：内核输出
- config：内核配置
- module：内核模块管理

### 3.4 mm/

负责内存管理。

- physical：物理页分配
- paging：虚拟内存与页表
- heap：内核堆
- vm：虚拟内存区域管理
- cache：内存缓存
- dma：DMA 内存管理

### 3.5 sched/ 与 process/

负责执行上下文与任务管理。

- process：进程管理
- thread：线程管理
- scheduler：调度器
- context：上下文管理
- sync：锁、信号量及同步原语
- timer：内核定时器

### 3.6 ipc/

负责进程间通信。

- message：消息传递
- pipe：管道
- shared_memory：共享内存
- event：事件与通知
- socket：本地通信接口

### 3.7 drivers/

负责设备驱动。

- core：驱动框架与设备模型
- bus：总线管理
- storage：存储控制器
- network：网卡驱动
- display：显示设备
- input：键盘、鼠标及触控
- audio：音频设备
- usb：USB
- bluetooth：蓝牙
- platform：平台设备
- mobile：移动设备调试与下载接口

驱动策略：
- 优先复用成熟、许可兼容的驱动实现。
- 自研设备模型、驱动管理和系统集成层。
- ADB、Fastboot、Qualcomm 9008/EDL、MTK 下载模式作为可选组件。
- 标准安装必须具备基本网络和显示支持。

### 3.8 fs/

负责文件系统。

- vfs：虚拟文件系统
- ramfs：内存文件系统
- devfs：设备文件系统
- procfs：进程与内核信息文件系统
- sysfs：设备与内核对象文件系统
- btrfs：Btrfs 文件系统支持
- fat：EFI 与兼容性文件系统
- core：文件系统注册、初始化与挂载管理

### 3.9 storage/

负责持久化存储基础设施。

- block：统一块设备接口
- partition：分区管理
- gpt：GPT 分区表
- disk：磁盘设备管理
- cache：块缓存
- volume：卷管理
- snapshot：快照管理

文件系统不得直接绑定某个具体磁盘控制器。

### 3.10 network/

负责网络协议与网络服务。

- core：网络核心
- ethernet：以太网
- ipv4：IPv4
- ipv6：IPv6
- tcp：TCP
- udp：UDP
- socket：套接字接口
- dns：域名解析
- dhcp：地址自动配置
- wifi：无线网络支持

### 3.11 syscall/

负责系统调用 ABI。

- interface：系统调用编号和参数定义
- entry：架构相关入口
- dispatch：系统调用分发
- validation：用户指针和参数校验

系统调用入口必须区分架构实现与通用分发逻辑。

### 3.12 runtime/

负责用户态程序运行环境。

- libc：基础 C 运行库
- loader：程序加载器
- elf：ELF 支持
- thread：用户态线程支持
- language：语言运行时集成

Java 开发环境作为系统软件环境的一部分规划，
包括 JDK、javac、java、jshell、jar、javadoc、JDB、
Maven 和 Gradle。

### 3.13 services/

负责系统后台服务。

- init：系统初始化服务
- device：设备管理服务
- network：网络管理服务
- storage：存储管理服务
- session：用户会话管理
- power：电源管理
- update：系统更新服务
- security：安全管理服务

### 3.14 shell/

负责命令行环境。

- qshell：自研 QShell
- parser：命令解析
- executor：命令执行
- pipeline：管道
- scripting：脚本
- builtin：内置命令

QShell 支持脚本、管道、对象数据流和系统管理能力。

### 3.15 desktop/

负责图形桌面系统。

- compositor：窗口合成
- window：窗口管理
- display：显示管理
- input：图形输入
- theme：主题
- session：桌面会话
- widget：基础控件
- shell：桌面 Shell

### 3.16 apps/

负责系统应用。

包括但不限于：

- QFiles
- QTerminal
- QCode
- QStore
- QSettings
- QCalculator
- QNote
- QDisk
- QBackup
- QDeviceLab
- QMonitor
- QMusic
- QVideo

应用的界面、交互和应用逻辑由 qmjyOS 自主开发。
底层通用库和协议实现可以合理复用。

### 3.17 package/

负责软件包管理。

- qpackage：软件包格式与管理
- qrepo：软件仓库
- qmirror：镜像管理
- qupdate：更新管理
- dependency：依赖解析
- signature：签名验证
- transaction：安装事务

必须验证软件包来源、签名、哈希、架构和依赖关系。

### 3.18 installer/

负责系统安装。

- partition：分区方案
- filesystem：文件系统创建
- install：系统部署
- bootloader：QBoot 安装
- account：用户初始化
- config：安装配置
- ui：安装界面

目标存储方案：
- ESP：FAT32
- 系统分区：Btrfs
- Btrfs 子卷：@、@var、@opt、@snapshots 等
- /home：独立物理分区，不占用根分区空间

### 3.19 recovery/

负责恢复环境。

- boot：恢复启动
- diagnose：系统诊断
- repair：系统修复
- backup：数据备份
- restore：系统恢复
- terminal：恢复终端

### 3.20 tools/

负责开发和维护工具。

- qbuild：统一构建工具
- qdebug：调试工具
- qtest：测试管理
- qimage：系统映像工具
- qemu：虚拟机运行辅助
- packaging：打包工具

## 4. 模块依赖原则

依赖方向必须保持清晰：

- arch 可以提供架构能力，但不能依赖桌面、应用和安装器。
- kernel 可以调用 arch、mm、sched、ipc 等内核子系统。
- drivers 可以使用 kernel、mm 和设备模型接口。
- storage 可以使用 drivers 提供的块设备接口。
- fs 可以使用 storage 提供的块设备接口。
- syscall 为用户态提供稳定的内核服务入口。
- runtime、services、shell、desktop 和 apps 属于用户态体系。
- installer、recovery 和 package 不得直接绕过内核接口操作硬件。

禁止出现：
- 内核依赖桌面应用。
- 驱动依赖具体应用。
- 文件系统直接操作具体硬件寄存器。
- 架构私有代码污染通用内核接口。
- 模块之间循环依赖。

## 5. 架构支持策略

第一阶段主线：
- x86_64
- BIOS 启动

必须规划的扩展：
- x86_64 UEFI
- ARM64 启动
- ARM64 内核架构接口

未完成的架构不得标记为已支持。

## 6. 构建体系

QBuild 是统一构建入口。

预期能力：
- build
- clean
- test
- config
- menuconfig
- image
- package
- run
- debug
- install

构建系统必须支持：
- 架构选择
- 启动固件选择
- 模块选择
- 编译参数统一管理
- 模块依赖管理
- 交叉编译工具链
- 构建产物管理

当前 Makefile 可以作为底层构建实现，逐步迁移到模块化构建规则。
不能在没有实际实现时宣称支持 UEFI 或 ARM64。

## 7. 测试体系

每个模块必须定义测试入口。

测试类型：
- unit：单元测试
- integration：集成测试
- boot：启动测试
- qemu：虚拟机测试
- abi：接口兼容性测试
- regression：回归测试

测试必须由 QBuild 统一调度。

## 8. 模块完成标准

模块只有满足以下条件才能标记为完成：

1. 职责和边界明确。
2. 公共头文件和接口已定义。
3. 依赖关系已记录。
4. 构建规则已接入。
5. 至少具有适当的测试入口。
6. 有可验证的实现或明确标记为接口阶段。
7. 文档与实际代码一致。

目录存在不等于模块完成。
头文件存在不等于功能完成。
编译通过不等于运行验证通过。

## 9. 第一部分验收目标

第一部分系统骨架建设结束前，必须完成：

- 全部主要模块目录与职责定义
- 公共接口和头文件规划
- 模块依赖关系
- x86_64 与 ARM64 架构接口规划
- QBoot 的 BIOS/UEFI 结构规划
- QBuild 模块化构建结构
- 模块测试入口规划
- 系统配置与版本管理基础
- 开发文档和模块完成状态清单

本文件是 qmjyOS 系统骨架的总体架构基线。
实际实现状态必须以源代码、构建结果和测试结果为准。
