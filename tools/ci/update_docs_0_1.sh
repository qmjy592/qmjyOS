#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(git rev-parse --show-toplevel 2>/dev/null)" || {
    echo "[ERROR] 当前目录不是 Git 仓库。"
    exit 1
}
cd "$ROOT"

STAMP="$(date +%Y%m%d_%H%M%S)"
BACKUP_DIR="build/docs-backup/$STAMP"
CHANGED=0
SKIPPED=0

append_section() {
    local file="$1"
    local marker="$2"

    if [[ -f "$file" ]] && grep -Fq "$marker" "$file"; then
        echo "[SKIP] 已存在更新标记：$file"
        SKIPPED=$((SKIPPED + 1))
        cat >/dev/null
        return 0
    fi

    mkdir -p "$(dirname "$file")"

    if [[ -f "$file" ]]; then
        mkdir -p "$BACKUP_DIR/$(dirname "$file")"
        cp -p "$file" "$BACKUP_DIR/$file"
    else
        : > "$file"
    fi

    {
        printf '\n'
        cat
        printf '\n'
    } >> "$file"

    echo "[UPDATED] $file"
    CHANGED=$((CHANGED + 1))
}

echo "========== qmjyOS 0.1.0 文档更新 =========="

# --------------------------------------------------
# 1. README.md
# --------------------------------------------------

append_section "README.md" "<!-- QMjyOS-DOCS-CI-14 -->" <<'EOF'
<!-- QMjyOS-DOCS-CI-14 -->

## 2026-11-16 Development and CI Status

### 中文

当前开发目标：在 2026-11-28 前完成 qmjyOS 0.1.0 的可验证发布版本。

本轮新增 CI 检查范围：

- Shell 和 Python 源码语法检查。
- QBuild 构建与配置检查。
- 不支持的平台目标拒绝检查。
- ELF32/ELF64 文件布局检查。
- AddressSanitizer 和 UndefinedBehaviorSanitizer 主机测试。
- QFS 主机测试及崩溃一致性测试。
- x86_64 BIOS 环境下的 QEMU 启动测试。

本地已确认通过：

- 源码语法检查。
- x86_64 BIOS QBuild 完整构建。
- ELF 布局检查。
- QBuild 测试套件。
- ASan/UBSan 主机测试。
- git diff --check。

注意：Termux 环境不支持 LeakSanitizer，因此本轮使用
detect_leaks=0。AddressSanitizer 和 UBSan 测试通过，
不代表内存泄漏检测已经通过。

GitHub Actions Run #14：

- Commit: 32cee5272d22385c0ac3fca8fee81b5a556585cc
- Run ID: 43826588727
- URL: https://github.com/qmjy592/qmjyOS/actions/runs/43826588727
- 最后记录的状态：代码与 QBuild 验证 Job 已通过；
  x86_64 BIOS 构建与 QEMU 测试 Job 当时仍在运行。
- 最终状态必须以 GitHub Actions 实际结果为准。

### English

The current target is a verifiable qmjyOS 0.1.0 release
by November 28, 2026.

New CI coverage includes source syntax checks, QBuild
validation, unsupported-target rejection, ELF layout
verification, ASan/UBSan host tests, QFS tests, and
x86_64 BIOS QEMU boot validation.

Local checks passed for source syntax, QBuild build,
ELF layout, QBuild tests, ASan/UBSan host tests, and
git diff --check.

LeakSanitizer is disabled on the current Termux
environment. Memory leak detection is therefore
not verified.

GitHub Actions Run #14:
- Commit: 32cee5272d22385c0ac3fca8fee81b5a556585cc
- Run ID: 43826588727
- URL: https://github.com/qmjy592/qmjyOS/actions/runs/43826588727

The recorded status was partial success, not a confirmed
complete CI pass. Check the live workflow result before
claiming completion.

### 0.1.0 Release Scope

The intended release scope includes:

- Kernel foundation and hardware initialization.
- Self-developed QBoot boot manager.
- Desktop environment and basic applications.
- Installer and recovery environment.
- Chinese and international English editions.

A planned component must not be described as implemented.
Every release component needs implementation evidence and
acceptance tests. Update this document when its status changes.
EOF

# --------------------------------------------------
# 2. PROJECT_OVERVIEW_CN_EN.md
# --------------------------------------------------

append_section "PROJECT_OVERVIEW_CN_EN.md" "<!-- QMjyOS-DOCS-CI-14 -->" <<'EOF'
<!-- QMjyOS-DOCS-CI-14 -->

## Development Status Update — 2026-11-16

### 中文：当前工程状态

当前已验证的开发基线：

- x86_64 BIOS 构建路径。
- 基础内核启动和串口输出。
- GDT、TSS、IDT 等 CPU 初始化基础。
- 物理内存管理、分页和堆管理基础组件。
- RAMFS/VFS 基础组件。
- 基础 Ring 3 特权级转换测试。
- Ring 0 协作式双任务调度测试。
- QBuild v0.5.0 构建与测试流程。

功能边界：

1. 当前 Ring 0 调度器是协作式调度，不是抢占式调度。
2. 当前进程模块的 metadata registry 不等于完整进程管理器。
3. 当前 Ring 3 测试不等于完整用户地址空间隔离。
4. ELF64 加载相关代码不等于完整、独立的进程地址空间。
5. 当前 BIOS 启动链仍属于开发阶段 bootstrap，
   不能等同于完整 QBoot 产品。
6. QBuild 已具备模块和构建管理能力，但底层仍依赖
   Makefile 构建流程。

### CI 状态

本地已通过源码语法、QBuild 构建、ELF 布局、
QBuild 测试、ASan/UBSan 主机测试和 git diff 检查。

Termux 不支持 LeakSanitizer，本地没有完成内存泄漏检测。

GitHub Actions Run #14：
https://github.com/qmjy592/qmjyOS/actions/runs/43826588727

最终 CI 状态需要以该运行记录为准。

### English: Current Engineering Status

The current verified development baseline includes:

- x86_64 BIOS build support.
- Basic kernel boot and serial output.
- GDT, TSS, and IDT initialization foundations.
- Physical memory, paging, and heap foundations.
- Basic RAMFS/VFS components.
- A basic Ring 3 privilege-transition test.
- A cooperative Ring 0 two-task scheduler test.
- QBuild v0.5.0 build and test workflows.

Important limitations:

1. The current scheduler is cooperative, not preemptive.
2. The process metadata registry is not a complete process manager.
3. The Ring 3 test does not prove complete user-space isolation.
4. ELF64 loading support does not prove independent process address spaces.
5. The current BIOS boot path is a development bootstrap,
   not a completed QBoot product.
6. QBuild still relies on Makefile-based underlying build rules.

Local checks passed for source syntax, QBuild build, ELF layout,
QBuild tests, ASan/UBSan host tests, and git diff validation.

LeakSanitizer is unsupported in the current Termux environment.
The final status of CI Run #14 must be checked separately.
EOF

# --------------------------------------------------
# 3. ARCHITECTURE_CN_EN.md
# --------------------------------------------------

append_section "ARCHITECTURE_CN_EN.md" "<!-- QMjyOS-DOCS-CI-14 -->" <<'EOF'
<!-- QMjyOS-DOCS-CI-14 -->

## Architecture Status and Implementation Boundaries

### 中文

当前架构状态应区分基础实现、实验性实现和计划实现。

**已经具备基础实现或测试证据的部分：**

- x86_64 BIOS 构建与启动路径。
- GDT/TSS/IDT 基础。
- 内存管理和分页基础。
- VFS/RAMFS 基础。
- Ring 3 基础特权级转换。
- Ring 0 协作式调度。
- QBuild v0.5.0。
- 主机端 ASan/UBSan 测试。

**仍需继续完成或验收的部分：**

- 完整的用户空间地址隔离。
- 完整的进程与线程管理。
- 抢占式任务调度。
- 稳定的系统调用 ABI。
- 完整的 QBoot 启动管理器。
- x86_64 UEFI 启动支持。
- AArch64 平台支持。
- 完整桌面环境及基础应用。
- 安装器和恢复环境。
- 全流程中英文语言切换。

### 调度和进程模型

当前双任务测试证明协作式任务可以交替执行，
不能据此宣称抢占式调度已完成。

进程 metadata registry 只提供进程元数据管理基础，
不代表已经实现完整 CR3 切换、独立地址空间、
进程隔离和完整生命周期管理。

### 用户态模型

Ring 3 基础转换测试证明 CPU 特权级转换路径可运行。
完整用户空间隔离、系统调用 ABI 和进程级资源保护
仍需要独立实现和测试。

### 启动模型

当前 x86_64 BIOS 启动路径是开发 bootstrap。
正式产品需要完成自研 QBoot，并独立验收 BIOS/UEFI
启动链路。不能将现有 bootstrap 描述为完整 QBoot。

### 桌面与应用模型

QDesktop、QWindow、QGraphics、QTheme、QIcon、
QDock、QStatus、QStart，以及文件管理器、设置、
终端和基础系统应用，在没有实际实现和测试之前
均应标记为计划或开发中。

### English

Architecture documentation distinguishes foundational,
experimental, and planned implementations.

Implemented foundations include the x86_64 BIOS build path,
GDT/TSS/IDT, memory-management foundations, VFS/RAMFS,
a basic Ring 3 transition test, cooperative Ring 0 scheduling,
QBuild v0.5.0, and host ASan/UBSan testing.

Outstanding work includes full user-space isolation,
complete process/thread management, preemptive scheduling,
a stable syscall ABI, a complete self-developed QBoot,
UEFI and AArch64 support, the desktop environment,
basic applications, installer, recovery, and end-to-end
Chinese/English localization.

A cooperative scheduler test is not evidence of preemptive
scheduling. A process metadata registry is not a complete
process/address-space implementation. A Ring 3 transition
test is not proof of complete isolation.

The current BIOS bootstrap must not be described as a
completed QBoot product.

Desktop components and applications remain planned until
their implementation and acceptance tests are available.
EOF

# --------------------------------------------------
# 4. BUILD_AND_TESTING_CN_EN.md
# --------------------------------------------------

append_section "BUILD_AND_TESTING_CN_EN.md" "<!-- QMjyOS-CI-EXPANSION-2026-11-16 -->" <<'EOF'
<!-- QMjyOS-CI-EXPANSION-2026-11-16 -->

## CI Expansion — 2026-11-16

### 新增检查项目

| 检查项目 | 检查内容 |
|---|---|
| Source syntax | Shell/Python 语法检查 |
| Unsupported targets | 不支持的架构/启动组合必须被拒绝 |
| ELF layout | ELF32/ELF64 class、架构、入口和 LOAD 段 |
| QBuild | 配置、构建、模块及测试 |
| ASan/UBSan | 主机端地址和未定义行为检查 |
| QFS | 主机测试和崩溃一致性测试 |
| QEMU | x86_64 BIOS 实际启动检查 |

### 本地执行命令

```bash
cd "$HOME/qmjyOS" || exit 1

bash tools/ci/check_sources.sh

./qbuild build --arch x86_64 --boot bios --jobs 4

bash tools/ci/verify_elf_layout.sh

./qbuild test --arch x86_64 --boot bios

bash tools/ci/run_host_sanitizers.sh

git diff --check
```

### 本地验证记录

用户已确认以上本地检查全部通过。

特别说明：

Termux 当前环境不支持 LeakSanitizer 的
detect_leaks 功能，因此 sanitizer 使用
detect_leaks=0。

ASan/UBSan 检查通过不代表 LeakSanitizer 检查通过，
也不能宣称已经完成全面内存泄漏检测。

### GitHub Actions Run #14

- Commit: 32cee5272d22385c0ac3fca8fee81b5a556585cc
- Run ID: 43826588727
- URL: https://github.com/qmjy592/qmjyOS/actions/runs/43826588727

本轮需要重点核实：

1. Code and QBuild validation。
2. 完整 x86_64 BIOS 构建。
3. ELF layout。
4. QBuild test suite。
5. ASan/UBSan。
6. QFS host/crash consistency。
7. 实际 QEMU BIOS boot。
8. 构建产物上传。

在没有读取最终 workflow 结果之前，
不得把 Run #14 记为全部通过。

### English

The expanded CI pipeline covers source syntax, unsupported
target rejection, ELF layout, QBuild, ASan/UBSan, QFS host
and crash-consistency tests, and actual x86_64 BIOS QEMU boot.

Local checks have passed.

LeakSanitizer is disabled because the current Termux
environment does not support detect_leaks. Memory leak
detection remains unverified.

GitHub Actions Run #14 must be checked for its final result:
https://github.com/qmjy592/qmjyOS/actions/runs/43826588727

Do not report the workflow as fully passing until every
required job and step has completed successfully.
EOF

# --------------------------------------------------
# 5. PHASE2_PHASE3_INTEGRATION.md
# --------------------------------------------------

append_section "docs/architecture/PHASE2_PHASE3_INTEGRATION.md" "<!-- QMjyOS-PHASE2-PHASE3-STATUS-UPDATE-2026-11-16 -->" <<'EOF'
<!-- QMjyOS-PHASE2-PHASE3-STATUS-UPDATE-2026-11-16 -->

## Phase 2 / Phase 3 Status Clarification — 2026-11-16

### 中文：实现边界

本节用于明确阶段集成进度，避免将基础组件、
实验性路径和完整产品能力混为一谈。

**启动与内存：**

- 当前 x86_64 BIOS 启动路径已有构建和启动测试基础。
- Multiboot 启动信息处理与物理内存初始化需要分别验收。
- ELF64 加载相关功能不能自动等同于完整独立地址空间。
- 页面权限、用户页映射和进程级 CR3 隔离必须单独验证。

**任务和进程：**

- 当前 Ring 0 双任务交替测试属于协作式调度。
- 抢占式调度仍需要单独实现和测试。
- 进程 metadata registry 不能代替完整进程管理。
- 独立地址空间、进程切换、线程生命周期和资源回收
  均需要独立验收。

**Ring 3：**

- 基础 CPL3 到 CPL0 特权级转换测试已有记录。
- 完整用户空间隔离和稳定 syscall ABI 仍需继续完成。
- 不应将一次特权级转换测试描述为完整用户态环境。

**文件系统：**

- QFS 的主机端测试和崩溃一致性检查应以实际测试结果为准。
- 持久化、日志、恢复和崩溃重放功能必须分别有实现证据。
- 不应仅凭构建成功推断所有断电恢复场景均可靠。

### CI 扩展记录

本轮新增源码语法、ELF 布局、不支持目标拒绝、
ASan/UBSan、QFS 和 QEMU 检查。

本地已通过的检查包括源码语法、QBuild 构建、
ELF 布局、QBuild 测试、ASan/UBSan 和
git diff --check。

Termux 不支持 LeakSanitizer，内存泄漏检测未完成。

Run #14：
https://github.com/qmjy592/qmjyOS/actions/runs/43826588727

最终 CI 状态需要独立核实。

### English: Integration Boundaries

This section separates foundational components,
experimental paths, and complete product capabilities.

The current x86_64 BIOS path has build and boot-test
foundations. Multiboot information handling and physical
memory initialization require separate verification.
ELF64 loading support does not automatically provide
independent process address spaces.

The Ring 0 two-task test demonstrates cooperative scheduling,
not preemptive scheduling. The process metadata registry
is not a complete process manager.

A basic CPL3-to-CPL0 transition test does not establish
complete user-space isolation or a stable syscall ABI.

QFS persistence, logging, recovery, and crash replay must
each be supported by implementation and test evidence.
A successful build alone does not prove power-loss safety.

The expanded CI includes source syntax, ELF layout,
unsupported target rejection, ASan/UBSan, QFS, and QEMU.
The final Run #14 result must be verified independently.
EOF

# --------------------------------------------------
# 6. 0.1.0 acceptance checklist
# --------------------------------------------------

append_section "docs/roadmap/0.1.0_ACCEPTANCE_CN_EN.md" "<!-- QMjyOS-0.1.0-ACCEPTANCE -->" <<'EOF'
<!-- QMjyOS-0.1.0-ACCEPTANCE -->

# qmjyOS 0.1.0 Acceptance Checklist

**Target release date:** 2026-11-28

**Status policy:**

- [x] Implemented and locally verified
- [ ] Implemented but not fully verified
- [~] In progress / experimental
- [ ] Planned / not yet implemented
- [!] Blocked or requires a decision

A checked item must have actual implementation and test evidence.
A design document or roadmap entry is not implementation evidence.

---

## 1. Kernel Foundation / 内核基础

| Item | Status | Acceptance |
|---|---|---|
| x86_64 BIOS build | [x] | Local build verified |
| Basic kernel boot | [x] | Existing QEMU boot evidence |
| Serial output | [x] | Boot output checked |
| GDT/TSS/IDT foundations | [x] | Initialization path |
| Physical memory management | [~] | Full boundary tests required |
| Paging foundation | [~] | User isolation still outstanding |
| Heap management | [~] | Full stress tests required |
| VFS/RAMFS foundation | [~] | Integration acceptance required |
| Persistent QFS | [~] | Persistence and crash recovery acceptance |

## 2. CPU, Tasks and Processes / CPU、任务与进程

| Item | Status | Acceptance |
|---|---|---|
| Basic Ring 3 transition | [x] | CPL3 transition test |
| Ring 0 cooperative scheduler | [x] | Task A/B alternation |
| Preemptive scheduler | [ ] | Timer-driven scheduling |
| Complete process manager | [ ] | Lifecycle and resource management |
| Independent process address spaces | [ ] | CR3 and mapping isolation |
| Complete user-space isolation | [ ] | Permission and fault tests |
| Stable syscall ABI | [ ] | Documented ABI and tests |
| Thread management | [ ] | Create, schedule, exit and cleanup |

## 3. QBoot and Platforms / 引导与平台

| Item | Status | Acceptance |
|---|---|---|
| x86_64 BIOS bootstrap | [x] | Existing development path |
| Self-developed QBoot manager | [ ] | Standalone boot manager |
| x86_64 UEFI | [ ] | UEFI boot test |
| AArch64 | [ ] | Platform build and boot test |
| Boot menu and recovery entry | [ ] | End-to-end test |

The existing BIOS bootstrap is not equivalent to a completed
QBoot product.

## 4. Desktop and Basic Applications / 桌面与基础应用

| Item | Status | Acceptance |
|---|---|---|
| QDesktop | [ ] | Working desktop session |
| QWindow | [ ] | Window lifecycle and interaction |
| QGraphics | [ ] | Rendering pipeline |
| QTheme/QIcon | [ ] | Theme and icon rendering |
| QDock/QStatus/QStart | [ ] | Working desktop shell |
| QComputer | [ ] | Basic system information |
| QFileManager | [ ] | File operations |
| QRecycleBin | [ ] | Delete and restore |
| QSettings | [ ] | Persistent settings |
| QShell | [ ] | Interactive command environment |
| AI entry point | [ ] | Functional integration |

All listed desktop components remain planned until
their implementation is demonstrated.

## 5. Installer and Recovery / 安装器与恢复环境

| Item | Status | Acceptance |
|---|---|---|
| QInstall CLI | [ ] | Complete install workflow |
| Live installer GUI | [ ] | Working installation interface |
| Offline installation | [ ] | Install without network |
| Disk and partition handling | [ ] | Safe partition tests |
| Hardware compatibility warnings | [ ] | Detection and warning tests |
| Recovery environment | [ ] | Boot and recovery test |
| Lite/Balanced/Full profiles | [ ] | Profile installation tests |

## 6. Language Editions / 语言版本

| Item | Status | Acceptance |
|---|---|---|
| Chinese edition | [~] | Simplified Chinese throughout |
| International edition | [~] | English throughout |
| Boot localization | [ ] | Boot menu translations |
| Installer localization | [ ] | Installer translations |
| Recovery localization | [ ] | Recovery translations |
| Desktop localization | [ ] | Full desktop translations |

The Chinese edition defaults to Simplified Chinese.
The international edition must be fully English, including
boot, recovery, installer, and system UI.

## 7. Build, CI and Release Quality / 构建、CI 与发布质量

| Item | Status | Acceptance |
|---|---|---|
| QBuild v0.5.0 | [x] | Existing local validation |
| Source syntax CI | [x] | Local check passed |
| Unsupported target rejection | [x] | Local validation |
| ELF layout check | [x] | Local check passed |
| QBuild build/test | [x] | Local checks passed |
| ASan/UBSan | [x] | Local checks passed |
| LeakSanitizer | [!] | Unsupported in current Termux |
| QFS CI tests | [~] | Run #14 final result required |
| QEMU BIOS CI boot | [~] | Run #14 final result required |
| Release image | [ ] | Reproducible image build |
| Release documentation | [~] | Final review required |

## 8. Release Gate / 发布门槛

The 0.1.0 release must satisfy all of the following:

1. The released image must build reproducibly.
2. The documented supported boot target must boot successfully.
3. Kernel, storage, and filesystem tests must pass.
4. The desktop and advertised basic applications must actually work.
5. Installer and recovery features must match release claims.
6. Chinese and English editions must match their language policy.
7. CI results and known limitations must be documented.
8. No planned feature may be falsely described as complete.

If the full planned scope cannot be completed by
2026-11-28, explicitly decide and document the release scope.
Do not disguise unfinished features as completed features.

---

## English Release Principle

A green CI pipeline alone does not prove that the entire OS
is ready for release. Every advertised feature requires
implementation evidence and an acceptance test.

## 中文发布原则

CI 全绿不等于整个操作系统已经完成。
所有对外宣称的功能都必须有真实实现和验收测试。
EOF

# --------------------------------------------------
# 7. 检查与统计
# --------------------------------------------------

echo
echo "========== 文档修改统计 =========="
echo "[INFO] 新增/更新记录数：$CHANGED"
echo "[INFO] 已存在而跳过数：$SKIPPED"

echo
echo "========== Markdown 文件检查 =========="

FILES=(
    "README.md"
    "PROJECT_OVERVIEW_CN_EN.md"
    "ARCHITECTURE_CN_EN.md"
    "BUILD_AND_TESTING_CN_EN.md"
    "docs/architecture/PHASE2_PHASE3_INTEGRATION.md"
    "docs/roadmap/0.1.0_ACCEPTANCE_CN_EN.md"
)

for f in "${FILES[@]}"; do
    if [[ ! -s "$f" ]]; then
        echo "[FAIL] 文件不存在或为空：$f"
        exit 1
    fi
    echo "[OK] $f"
done

echo
echo "========== 更新标记检查 =========="

for marker in \
    "QMjyOS-DOCS-CI-14" \
    "QMjyOS-CI-EXPANSION-2026-11-16" \
    "QMjyOS-PHASE2-PHASE3-STATUS-UPDATE-2026-11-16" \
    "QMjyOS-0.1.0-ACCEPTANCE"
do
    if grep -R -Fq "$marker" \
        README.md \
        PROJECT_OVERVIEW_CN_EN.md \
        ARCHITECTURE_CN_EN.md \
        BUILD_AND_TESTING_CN_EN.md \
        docs/architecture/PHASE2_PHASE3_INTEGRATION.md \
        docs/roadmap/0.1.0_ACCEPTANCE_CN_EN.md
    then
        echo "[OK] $marker"
    else
        echo "[FAIL] 缺少标记：$marker"
        exit 1
    fi
done

echo
echo "========== Git diff 检查 =========="
git diff --check

echo
echo "========== 修改摘要 =========="
git status --short
git diff --stat

if [[ -d "$BACKUP_DIR" ]]; then
    echo
    echo "[BACKUP] 原有文档备份：$BACKUP_DIR"
else
    echo
    echo "[INFO] 本轮没有备份文件。"
fi

echo
echo "[PASS] 文档增量更新和基础检查完成。"
echo "[NOTE] 尚未提交或上传 GitHub。"
