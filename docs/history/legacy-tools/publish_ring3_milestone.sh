#!/data/data/com.termux/files/usr/bin/bash
set -Eeuo pipefail

cd "$HOME/mykernel-stage1-test/ring3_demo"

echo "===== qmjyOS Ring3 里程碑发布 ====="

if [ "$(git branch --show-current)" != "main" ]; then
    echo "[ERROR] 当前分支不是 main，停止发布。"
    exit 1
fi

git fetch origin

if ! git merge-base --is-ancestor origin/main HEAD; then
    echo "[ERROR] 远端 main 有本地尚未包含的提交。"
    echo "为避免覆盖远端历史，停止发布。"
    exit 1
fi

cat > RING3_VALIDATION_2026-11-10.md <<'REPORT'
# qmjyOS Ring 3 里程碑验收记录

日期：2026-11-10

## 本次成果

Ring 3 基础特权级切换与系统调用返回链路已经在 QEMU 中实际运行通过。
返回 Ring 0 后，原有两个协作式任务继续交替运行。

## 构建和测试

- make clean && make：通过
- make test-memory：ALL TESTS PASSED
- make test-paging：Paging self-test: PASS
- make test-boot：启动链静态检查全部通过
- ring3_entry.o、task.o、switch.o 均参与内核链接

## QEMU 运行验收记录

关键串口输出：

\`\`\`text
Physical page allocator self-test: PASS
Paging manager self-test: PASS
GDT and TSS initialized.

[Ring 3] Preparing isolated user address space.
[Ring 3] User code page: RX, U/S=1.
[Ring 3] User stack page: RW/NX, U/S=1.
[Ring 3] Kernel identity mapping: supervisor-only.
[Ring 3] CR3 activated.
[Ring 3] Entering CPL3.
[Ring 3] System call received successfully.
[Ring 3] Returning to Ring 0.
[Ring 3] Returned to Ring 0 successfully.
[Ring 3] Basic privilege transition: PASS.
Cooperative scheduler initialized (2 tasks).
Starting cooperative Ring 0 scheduler.
[task A] running
[task B] running
[task A] running
[task B] running
\`\`\`

## 已验证

1. Multiboot 内存映射和物理页分配器自测试。
2. 页表管理器自测试。
3. GDT 和 TSS 初始化。
4. 用户代码页 RX、U/S=1。
5. 用户栈页 RW/NX、U/S=1。
6. 内核身份映射 Supervisor-only。
7. CR3 激活。
8. Ring 0 进入 CPL3。
9. int 0x80 系统调用成功。
10. 系统调用返回 Ring 0。
11. Ring 3 测试结束后 Ring 0 Task A/B 持续交替运行。

## 尚未完成

本次验收不代表完整进程隔离或生产级安全已经完成。

后续还需验证：

- Ring 3 写入只读代码页触发 Page Fault。
- Ring 3 访问 Supervisor 内核页触发 Page Fault。
- Ring 3 执行 NX 栈触发 Page Fault。
- Page Fault 错误码、CR2 和异常处理。
- 多用户地址空间切换与资源回收。
- 抢占式调度和完整系统调用 ABI。

当前调度器仍是 Ring 0 协作式双任务调度器。
REPORT

echo "[INFO] 验收报告已生成。"

git add -A src Makefile tests tools
git add RING3_VALIDATION_2026-11-10.md

if git diff --cached --quiet; then
    echo "[INFO] 没有新的源码变更，检查是否只需发布验收报告。"
    if git ls-files --error-unmatch RING3_VALIDATION_2026-11-10.md >/dev/null 2>&1; then
        echo "[INFO] 报告已经被跟踪且没有变化。"
        exit 0
    fi
fi

git diff --cached --stat

git commit -m "feat: validate Ring 3 privilege transition and scheduler"

echo "===== 推送 GitHub ====="
git push origin main

echo "===== 发布完成 ====="
git log -1 --oneline
