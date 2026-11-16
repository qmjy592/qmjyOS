# Build, Run and Testing Guide / 构建、运行与测试说明

## 1. Toolchain / 工具链

The current Makefile expects:

当前 Makefile 预期需要：

- `clang`: compiles freestanding 32-bit and 64-bit objects.
  `clang`：编译 freestanding 32 位和 64 位目标文件。
- `ld.lld`: links the 32-bit bootstrap image and the 64-bit kernel image.
  `ld.lld`：链接 32 位引导映像和 64 位内核映像。
- `llvm-objcopy`: converts the 64-bit ELF into a flat binary and embeds it as a binary object.
  `llvm-objcopy`：把 64 位 ELF 转换为平坦二进制，并将其嵌入为二进制目标文件。
- `make`: runs the build rules.
  `make`：执行构建规则。
- `qemu-system-x86_64`: runs the image for emulation, if available.
  `qemu-system-x86_64`：在可用时运行模拟器测试。

Tool package names differ between Linux distributions and Termux. Verify the actual executable names with `command -v`.

不同 Linux 发行版和 Termux 的软件包名称可能不同。请用 `command -v` 确认实际可执行文件名称。

## 2. Build / 构建

From the repository root:

在仓库根目录执行：

```sh
make clean
make
```

The build has two linked stages. `kernel64.elf` is linked with `linker64.ld` at 2 MiB. `kernel64.bin` is extracted from that ELF. The binary is converted into `kernel64.o`, which is embedded into the 32-bit outer image. `kernel.elf` is then linked with `linker32.ld`, keeping the Multiboot header near the start of the image.

构建分为两个链接阶段。`kernel64.elf` 使用 `linker64.ld` 链接到 2 MiB；随后提取为 `kernel64.bin`。该二进制被转换为 `kernel64.o` 并嵌入 32 位外层映像。最后使用 `linker32.ld` 链接生成 `kernel.elf`，并把 Multiboot 头保留在映像靠前位置。

A successful `make` proves that the configured compiler and linker accepted the source and produced the target. It does not prove that the bootloader recognizes the image, that long-mode entry is correct, that interrupts return safely, or that memory isolation is secure.

`make` 成功只能证明当前编译器和链接器接受了源码并生成目标文件。它不能证明引导器能够识别映像、长模式入口正确、中断能够安全返回，或内存隔离具备安全性。

## 3. QEMU run / QEMU 运行

If QEMU is installed and supports the selected image format:

如果已安装 QEMU 且支持当前映像格式：

```sh
make run
```

The Makefile currently invokes:

Makefile 当前执行的命令为：

```sh
qemu-system-x86_64 -kernel kernel.elf -nographic -no-reboot
```

Capture the full serial output. Check whether the kernel banner, memory allocator messages, paging self-test result, GDT/TSS message, scheduler startup message and task output appear. Do not infer success from QEMU merely opening or from a linker exit code.

请保存完整串口输出。检查是否出现内核启动标志、内存分配器信息、分页自测试结果、GDT/TSS 信息、调度器启动信息和任务输出。不能仅凭 QEMU 窗口打开或链接器退出码推断系统运行成功。

## 4. Host-side memory allocator test / 主机侧内存分配器测试

Run:

执行：

```sh
make test-memory
```

This builds `tests/memory_host_test.c` with `mm/physical/memory.c` as a normal host process. The test maps fake Multiboot information and memory-map buffers at low virtual addresses, then checks parsing, range overlap accounting, reservations, allocation, freeing, double-free rejection, reinitialization and malformed-map handling.

该测试将 `tests/memory_host_test.c` 与 `mm/physical/memory.c` 编译为普通主机进程。测试会在低虚拟地址映射伪造的 Multiboot 信息和内存映射缓冲区，然后检查解析、区间重叠计数、保留区间、分配、释放、重复释放拒绝、重新初始化和错误映射处理。

The test may return exit code 77 with a SKIP message if the host disallows the requested low-address mappings. SKIP is not the same as PASS; it means the environment prevented the test from running.

如果主机不允许所请求的低地址映射，测试可能返回退出码 77 并显示 SKIP。SKIP 不等于 PASS，它表示环境限制导致测试没有运行。

Optional UndefinedBehaviorSanitizer test:

可选的 UndefinedBehaviorSanitizer 测试：

```sh
mkdir -p build/test-evidence
gcc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=undefined \
  -fno-sanitize-recover=all -I. mm/physical/memory.c tests/memory_host_test.c \
  -o build/test-evidence/qmjyOS-memory-test-ubsan
build/test-evidence/qmjyOS-memory-test-ubsan
```

## 5. What each test proves / 各测试能证明什么

| Test / 测试 | Evidence / 证据 | Does not prove / 不能证明 |
|---|---|---|
| `make` | Compilation and linking / 编译与链接 | Runtime correctness / 运行正确性 |
| QEMU boot | Boot path reaches observed output / 启动路径到达实际观察到的输出 | Complete hardware compatibility or security / 完整硬件兼容性或安全性 |
| Physical allocator host test | Allocator parsing/accounting behavior under harness / 测试框架下分配器解析和计数行为 | Real boot memory-map correctness on every machine / 所有真实机器上的引导内存映射正确性 |
| `vm_self_test` and Ring 3 test | Page-table data-structure checks plus the observed CR3 activation and basic CPL3/syscall round trip / 页表数据结构检查，以及实际观察到的 CR3 激活、CPL3 和系统调用往返 | Complete process isolation, all TLB edge cases or security / 完整进程隔离、所有 TLB 边界情况或安全性 |
| Cooperative task output | Tasks yield and resume in observed QEMU run / 观察到的 QEMU 运行中任务让出并恢复 | Timer preemption or SMP / 定时器抢占或多核调度 |
| GDT/TSS and Ring 3 runtime output | GDT/TSS setup and the observed basic privilege transition / GDT/TSS 初始化及实际观察到的基础特权级切换 | Complete user-mode ABI or security audit / 完整用户态 ABI 或安全审计 |

## 6. Recommended test sequence / 推荐测试顺序

1. Clean build: `make clean && make`.
   清理并构建：`make clean && make`。
2. Run host test: `make test-memory`.
   运行主机测试：`make test-memory`。
3. Boot QEMU and save the entire serial log.
   启动 QEMU 并保存完整串口日志。
4. Confirm the memory allocator self-test result and paging self-test result separately.
   分别确认物理内存分配器自测试和分页自测试结果。
5. Confirm timer interrupts are acknowledged and the tick counter advances, without describing this as preemption.
   确认定时器中断得到确认且 tick 递增，但不要把它描述为抢占。
6. Confirm both cooperative tasks make progress and alternate through explicit yields.
   确认两个协作任务都能推进，并通过显式 yield 交替运行。
7. Run the existing basic Ring 3 transition test; continue testing protection faults, TSS stack switching, exception frames and syscall ABI before claiming complete user-mode isolation.
   运行现有基础 Ring 3 切换测试；在宣称完整用户态隔离前，继续验证保护异常、TSS 栈切换、异常栈帧和系统调用 ABI。

## 7. Diagnostics to record / 建议记录的诊断信息

For each test run, record:

每次测试建议记录：

- Git commit SHA and branch.
  Git 提交 SHA 和分支。
- Termux/host OS version and CPU architecture.
  Termux/宿主系统版本和 CPU 架构。
- Compiler, linker, objcopy and QEMU versions.
  编译器、链接器、objcopy 和 QEMU 版本。
- Exact command line.
  完整命令行。
- Exit code.
  退出码。
- Full serial output, including the last line before a hang or reset.
  完整串口输出，包括卡死或重启前的最后一行。
- Whether the test was PASS, FAIL, or SKIP.
  测试结论是 PASS、FAIL 还是 SKIP。

## 8. Safety notes / 安全注意事项

Do not enable Ring 3 simply by changing a page-table flag. User access depends on the U/S bit through every paging level, correct address-space contents, supervisor-only kernel mappings, a valid TSS kernel stack, correct exception entry/return frames and a controlled syscall interface. NX also requires CPU feature detection and enabling EFER.NXE before setting NX PTE bits.

不要仅通过修改一个页表标志就启用 Ring 3。用户访问权限取决于每一级页表的 U/S 位、正确的地址空间内容、仅内核可访问的内核映射、有效的 TSS 内核栈、正确的异常入口/返回栈帧，以及受控的系统调用接口。NX 还要求先检测 CPU 特性并启用 EFER.NXE，之后才能设置 PTE 的 NX 位。

This project is experimental. Do not use it as a secure operating system or to store sensitive data.

本项目仍属实验性质，不要将其作为安全操作系统使用，也不要用来存储敏感数据。

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


<!-- QBUILD-1.0.0-VALIDATION-2026-11-16 -->

## QBuild 1.0.0 validation / QBuild 1.0.0 验证

QBuild 1.0.0 extends the existing Makefile frontend with safe module configuration changes, hash-verified configuration snapshots and rollback-aware restore, build/test JSON reports, toolchain diagnostics, and a built-in regression suite. Run `./qbuild doctor`, `./qbuild self-test`, and `./qbuild test --arch x86_64 --boot bios --jobs 2`. Build/test logs and reports are stored under the ignored `build/qbuild/reports/`; configuration snapshots are stored under the ignored `build/qbuild/snapshots/`.

QBuild 1.0.0 新增安全模块配置修改、SHA-256 快照与恢复回滚、构建/测试 JSON 报告、工具链诊断和自身回归测试。建议执行 `./qbuild doctor`、`./qbuild self-test`、`./qbuild test --arch x86_64 --boot bios --jobs 2`。构建/测试日志及报告保存在已忽略的 `build/qbuild/reports/`，配置快照保存在已忽略的 `build/qbuild/snapshots/`。

The supported target remains x86_64 + BIOS. A local PASS does not imply GitHub Actions PASS; validate the pushed commit's workflow separately. QBuild 1.0.0 and the qmjyOS system release version are independent.

当前支持目标仍为 x86_64 + BIOS。本地 PASS 不等于 GitHub Actions PASS；推送后必须单独核验对应提交的 CI。QBuild 1.0.0 与 qmjyOS 操作系统发布版本相互独立。
