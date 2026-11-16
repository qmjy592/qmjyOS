# qmjyOS Phase 2/3 v3 API 命名空间修正版

## 修复原因

原 v3 补丁把内核核心初始化注册接口命名为 `qm_init_register`、`qm_init_entry_t`、`qm_init_level_t`，与仓库已有的 `init/startup/qm_init.c` / `qm_init.h` 服务初始化注册系统发生全局符号及类型命名冲突。

本热修复保留原有服务初始化 API 不变，仅将新增的内核核心初始化 API 统一改为 `qm_kernel_init_*`，并将阶段常量统一改为 `QM_KERNEL_INIT_*`。修复覆盖 v3 补丁载荷、测试、文档和安装器中的相关标识符。

## 使用前提

原 v3 ZIP 已经解压在 `~/qmjyOS` 根目录，目录结构为：

```
~/qmjyOS/qmjyOS_phase2_phase3_progress_v3/apply_phase2_all_v3.sh
```

把本修正版 ZIP 解压到 `~/qmjyOS` 根目录后执行：

```
bash qmjyOS_phase2_phase3_v3_namespace_fix/apply_v3_namespace_hotfix.sh
```

脚本先对原 v3 补丁包做预检，备份将要改写的补丁文件到 `build/repair-backups/phase2-v3-api-namespace/`，再进行原 v3 安装流程。不会执行 git reset/checkout/clean，也不使用 `/tmp`。

若只想修正解压目录中的补丁载荷、不立即应用项目源码：

```
bash qmjyOS_phase2_phase3_v3_namespace_fix/apply_v3_namespace_hotfix.sh --patch-only
```

然后可检查后再执行：

```
bash qmjyOS_phase2_phase3_progress_v3/apply_phase2_all_v3.sh
```

若预检发现新命名空间也已存在定义，脚本会停止，不会继续应用。原有 `qm_init_*` 服务 API 不会被重命名或删除。
