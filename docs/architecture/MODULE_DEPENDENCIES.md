# qmjyOS 模块依赖规范

## 一、总体分层

系统采用分层架构：

1. Boot：QBoot 和启动协议。
2. Architecture：CPU、寄存器、中断控制器、上下文切换。
3. Kernel Core：内核初始化、日志、异常、模块管理。
4. Memory：物理内存、虚拟内存、堆、缓存。
5. Process/Scheduler：进程、线程、调度和同步。
6. IPC：进程间通信。
7. Driver：设备模型和硬件驱动。
8. Storage：块设备、分区、卷和磁盘管理。
9. Filesystem：VFS 和具体文件系统。
10. Network：网络设备抽象和协议栈。
11. Syscall：内核与用户态的系统调用边界。
12. Runtime：用户态运行库和程序加载器。
13. Services：系统服务。
14. Shell/Desktop：命令行和图形界面。
15. Applications：系统应用。
16. Package/Installer/Recovery：软件包、安装和恢复。
17. Development Tools：构建、调试和测试工具。

## 二、允许的主要依赖

Boot
  -> Architecture
  -> Kernel Core

Architecture
  -> Kernel Core
  -> Memory

Kernel Core
  -> Architecture
  -> Memory
  -> Scheduler
  -> IPC

Memory
  -> Architecture
  -> Kernel Core

Scheduler
  -> Architecture
  -> Memory
  -> Kernel Core

IPC
  -> Kernel Core
  -> Memory
  -> Scheduler

Drivers
  -> Kernel Core
  -> Memory
  -> Architecture

Storage
  -> Kernel Core
  -> Memory
  -> Block Device Interfaces

Filesystem
  -> Kernel Core
  -> Memory
  -> Storage Interfaces

Network
  -> Kernel Core
  -> Memory
  -> Network Device Interfaces

Syscall
  -> Architecture
  -> Kernel Core
  -> Process
  -> Memory

Runtime
  -> Syscall
  -> User-space Libraries

Services
  -> Runtime
  -> Syscall

Shell
  -> Runtime
  -> Syscall
  -> Services

Desktop
  -> Runtime
  -> Display/Input Interfaces
  -> Services

Applications
  -> Runtime
  -> Syscall
  -> Services

Package Manager
  -> Filesystem Interfaces
  -> Network Interfaces
  -> Cryptographic Interfaces

Installer
  -> Storage Interfaces
  -> Filesystem Interfaces
  -> Boot Interfaces

Recovery
  -> Storage Interfaces
  -> Filesystem Interfaces
  -> Diagnostic Interfaces

Development Tools
  -> Build Configuration
  -> Toolchain
  -> Test Infrastructure

## 三、禁止的依赖

1. Architecture 不得依赖具体应用程序。
2. Kernel Core 不得依赖桌面或图形应用。
3. 驱动不得直接调用应用程序接口。
4. 文件系统不得直接依赖某个具体磁盘 UI。
5. 应用程序不得直接访问硬件寄存器。
6. 用户态服务不得绕过系统调用边界直接调用内核内部实现。
7. 底层模块不得反向依赖上层模块。
8. 不允许模块之间形成循环依赖。

## 四、跨架构规则

公共接口不得暴露 x86_64 专用寄存器或结构。

架构专用实现放在：

- arch/x86_64/
- arch/aarch64/

跨架构接口放在：

- include/qmjy/
- arch/common/

需要架构差异时，通过架构接口分派，不允许在所有模块中散布架构判断。

## 五、依赖管理原则

- 模块通过公共头文件和稳定接口交互。
- 具体实现不得随意暴露内部数据结构。
- 驱动通过设备模型注册。
- 文件系统通过 VFS 注册。
- 存储通过块设备接口提供服务。
- 网络协议栈通过网络设备接口工作。
- 模块启用和禁用由统一配置管理。
- 构建系统必须检查模块依赖和目标架构。
