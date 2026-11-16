# qmjyOS Phase 2 Header API Fix

## 根因
Phase 2 foundation 的实现与测试已经切换到 `qm_kernel_init_*` 命名空间，
但现有 `include/qmjy/kernel.h` 仍保留旧的 `qm_init_*` 类型与函数声明。
因此编译器无法识别实现和测试使用的新类型、常量及函数。

## 使用
```bash
cd ~/qmjyOS || exit 1
bash qmjyOS_phase2_header_api_fix/fix_phase2_kernel_header_api.sh
```

## 安全行为
- 只对 `include/qmjy/kernel.h` 中精确且唯一的旧类型/声明锚点执行替换。
- 若文件处于部分修改状态或锚点不唯一，脚本会停止，不会猜测性写入。
- 修改前备份到 `build/repair-backups/phase2-header-api-fix/`。
- 使用同目录临时文件和原子替换。
- 不执行 git reset、checkout、clean，不删除任何目录。
- 随后编译并运行 foundation host tests，执行 `make -j2 all` 和 `git diff --check`。

注意：此脚本不代表整个 Phase 2/3 已完成，也不执行 MM_HEAP 或 BIOS NXE 阶段。
