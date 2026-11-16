# 修正版变更记录

- 保留现有 `qm_init_register(name, stage, priority, callback)` 服务初始化 API。
- 新增内核核心初始化注册接口使用 `qm_kernel_init_register`。
- 新增类型改为 `qm_kernel_init_level_t`、`qm_kernel_init_fn_t`、`qm_kernel_init_entry_t`。
- 新增阶段常量改为 `QM_KERNEL_INIT_EARLY`、`QM_KERNEL_INIT_ARCH`、`QM_KERNEL_INIT_MEMORY`、`QM_KERNEL_INIT_CORE`、`QM_KERNEL_INIT_DRIVER`、`QM_KERNEL_INIT_FILESYSTEM`、`QM_KERNEL_INIT_LATE`。
- `qm_init_run_level` 改为 `qm_kernel_init_run_level`。
- 修正安装器的重复定义预检目标，避免把仓库原有 `qm_init_register` 误判为新实现冲突。
- 安装器在修改补丁载荷前执行预检，并备份补丁载荷。
