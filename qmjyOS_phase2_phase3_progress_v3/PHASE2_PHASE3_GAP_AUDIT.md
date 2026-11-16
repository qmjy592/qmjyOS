# qmjyOS 第二、第三阶段当前缺口审计

> 审计输入：当前会话上传的 `qmjyos-phase13-local-sources.zip`（29 个文件，ZIP CRC 检查通过）、可读取的 `qmjy592/qmjyOS` `main` 分支公开源码与项目文档。
>
> 注意：本审计不把 GitHub `main` 自动等同于 Termux 本地全部源码。未包含在 ZIP 中的文件只按仓库公开版本观察。

## 1. 已确认的第一阶段基线

项目 README 与 `docs/development/PHASE1_COMPLETION.md` 将第一阶段定义为系统骨架/系统基础建设，并记录 2026-11-11 的本地验收：QBuild 配置、完整构建、PMM、分页、VFS、RAMFS、接口头文件、BIOS 静态映像检查等通过。该验收不声称 QEMU 完整启动或完整安全隔离已通过。

## 2. 第二阶段的主要缺口

| 子系统 | 上传配置/源码反映的状态 | 需要完成的内容 |
|---|---|---|
| Kernel init | 公共 `qmjy/kernel.h` 已有初始化分级接口，但当前 `main.c` 仍走直接硬编码初始化路径 | 引导信息验证、分级 init registry、失败传播、初始化顺序和日志 |
| PMM/VMM | PMM 与分页管理器存在；模块状态为 experimental | PMM 与页表页所有权整合、虚拟地址布局、页表分配与回滚、CR3/TLB 目标验证 |
| Kernel heap | `MM_HEAP=planned` | `kmalloc/kfree`、对齐、溢出、合并、耗尽和释放测试 |
| GDT/TSS/IDT | 汇编和 C 入口存在；上传的 `gdt_load.S` 已有 `lgdt` + `lretq` | 过渡期 IDT selector 与新 GDT 的衔接、TSS 栈生命周期、异常帧规范化、CPL3 fault containment |
| Scheduler | `SCHEDULER=experimental`；已有双任务协作式切换 | 任务状态/队列/阻塞唤醒/退出回收；先保留协作式基线，再验证抢占式 IRQ 帧切换 |
| Process/thread | `PROCESS`、`THREAD` 为 planned | PCB/TCB、独立地址空间、线程栈、生命周期和回收 |
| Syscall | `SYSCALL=planned`；当前 Ring 3 验证为单一测试调用 | ABI 版本、系统调用表、参数检查、用户指针 copy-in/copy-out、错误返回规则 |
| Storage/filesystem | QFS/IDE/MBR 等有代码路径，状态仍为实验性或规划状态 | 统一块设备接口、QFS crash consistency/recovery、设备错误和持久化测试 |
| QTest/runtime verification | 存在 host tests 和 QBoot/QFS 脚本 | 统一测试状态/证据与 QEMU 回归；SKIP 必须与 PASS 分开 |

### 重要边界

- 当前调度器的 `task_yield()` 是显式协作让出；定时器 IRQ 仅计 tick 不能称作抢占。
- Ring 3 入口与 `int 0x80` 测试调用证明的是基础往返，不等同于通用 syscall ABI 或多进程隔离。
- `kernel/core/main.c` 中已有的早期用户代码/旧 `enter_user_mode` 路径应先确认是否仍有引用，再决定删除；不应和新 `ring3_enter` 测试混用。
- 物理分配器限制在 1 GiB 低端窗口，与当前 bootstrap identity map 一致；扩展管理范围前必须先扩展映射/直接映射设计。

## 3. 第三阶段的主要缺口（新增规划）

| 子系统 | 当前状态 | 需要完成的内容 |
|---|---|---|
| ELF64 loader | 未找到进入构建计划的完整加载器 | ELF 段验证、页权限、用户栈、失败回滚 |
| Process lifecycle | `PROCESS` planned | spawn/exit/wait，后续再评估 fork/exec 兼容语义 |
| Kernel IPC | 公共接口与实验性消息队列代码不能直接视为同一运行时实现 | endpoint 所有权、句柄权限、队列限额、超时/阻塞、调度器唤醒 |
| Runtime | `RUNTIME` planned | 用户启动入口、基本运行库、系统调用包装 |
| Init/service lifecycle | manager/registry/logging/config 有实现和宿主机测试，状态 experimental | 从 host-tested 组件接入用户进程与 IPC，并完善依赖失败回滚 |
| QSHELL | planned | 用户态命令执行、终端 I/O、路径访问，再增加 pipe/redirection/job control |
| Device pseudo-filesystems | devfs/procfs/sysfs planned | 对设备、进程和系统状态提供 VFS 视图 |
| System integration | 尚未构成完整可启动用户系统 | QBoot → kernel → init → services → shell 的端到端运行验收 |

## 4. 本轮已完成的基础核对

- 上传 ZIP：29 个条目，`ZipFile.testzip()` 返回无损坏条目。
- 上传脚本 `tools/qboot/test_qfs_ide.sh`、`test_bios_boot.sh`、`build_bios_image.sh`、`build_qfs_ide_images.sh`：当前容器 `bash -n` 均通过。
- 上传环境中可找到 Clang 与 LLD；当前容器没有 `qemu-system-x86_64`，因此本轮不能执行 QEMU boot/runtime 验证。
- 上传的 `tools/qboot/test_qfs_ide.sh` 已保留 runtime 证据目录，和仓库旧版本使用 `rm -rf "$TEST_DIR"` 的清理实现不同；本地保留行为不可被旧版覆盖。

## 5. 实施优先级

1. P2 初始化/boot handoff 和 GDT/IDT/异常帧；这些是所有下游测试的可靠基座。
2. P2 PMM/VMM/heap；明确页所有权和地址布局。
3. P2 TCB/scheduler/IRQ 上下文，建立抢占前的帧合同。
4. P2 syscall dispatch 与安全用户指针访问。
5. P3 ELF loader、进程生命周期和独立用户地址空间。
6. P3 IPC/capability，再把已有 service manager/registry/log/config 接入运行时。
7. P3 init 与 shell，进行端到端启动和文件系统验收。

在 QEMU runtime 验证环境不可用时，相关项目只能标记为源码检查/构建验证/主机测试，不能标记为运行时通过。
