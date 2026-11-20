# Host-side tests / 主机侧测试

The host-side tests exercise selected kernel subsystems without booting QEMU.

主机侧测试用于在不启动 QEMU 的情况下检查部分内核子系统。

## 1. Physical memory allocator / 物理内存分配器

Run from the project root:

    make test-memory

This builds tests/memory_host_test.c with mm/physical/memory.c.

The test checks Multiboot memory-map parsing, overlapping usable ranges,
reservations, allocation, freeing, double-free rejection, reinitialization,
malformed maps and rollback behavior.

该测试检查 Multiboot 内存映射解析、可用区域重叠、保留区间、
页面分配与释放、重复释放拒绝、重新初始化、错误映射和回滚行为。

## 2. Paging manager / 分页管理器

Run:

    make test-paging

This runs the host-side paging manager self-test.

It validates selected inactive page-table data-structure operations.
It does not prove CR3 switching, hardware TLB behavior or complete
address-space isolation.

该测试检查主机侧分页管理器的数据结构操作，但不能证明 CR3 切换、
硬件 TLB 行为或完整地址空间隔离正确。

## 3. Boot image / 启动映像

Run:

    make test-boot

This performs static checks on the generated boot images.

It is not a substitute for an actual BIOS or UEFI boot test
in an emulator or on hardware.

该测试对生成的启动映像执行静态检查，不能替代在模拟器或真实硬件上的
BIOS/UEFI 实际启动测试。

## 4. Public interface header test / 公共接口头文件测试

Run:

    make test-interfaces

This test compiles all public qmjyOS headers together using the host compiler.

It checks basic header compatibility and declaration consistency only.
It does not test the runtime implementation of the declared interfaces.

该测试使用主机编译器一起编译 qmjyOS 公共头文件，
只检查头文件兼容性和声明一致性，不验证接口的运行时实现。

## 5. Limitations / 限制

A host-side test may be unavailable on a host that disallows
the requested low-address memory mappings.

A SKIP result is not a PASS.

如果主机不允许测试所需的低地址内存映射，主机测试可能显示 SKIP。

SKIP 不等于 PASS。

## 6. Test framework status / 测试框架状态

The current Makefile targets include host-side regression tests, boot-image static checks, and QEMU runtime tests for BIOS/UEFI when the required emulator and firmware are available.

The unified QTest runtime framework remains planned.

当前 Makefile 测试目标包括主机侧回归测试、启动映像静态检查，以及在工具/固件齐备时执行 BIOS/UEFI QEMU 运行测试。

统一的 QTest 运行时框架仍处于规划阶段。

See tests/qtest/README.md for the QTest implementation plan.

- `make test-process-runtime`
- `make test-scheduler-aspace`

- `make test-syscall-x86_64-abi` — x86_64 Ring 3 syscall trap-frame argument/return ABI

## Phase 3 runtime test coverage (2026-11-21)

The following host tests are part of QBuild's BIOS test selection:

- `make test-process-runtime`: ELF image to private process address-space runtime and user stack mapping checks.
- `make test-scheduler-aspace`: task/address-space CR3 association and switching checks under the host test harness.
- `make test-syscall-x86_64-abi`: x86_64 syscall trap-frame mapping and argument/result checks.

These tests are host-side. The QEMU BIOS/UEFI scripts additionally require the actual serial markers for private CR3 activation, CPL3 entry, process exit/page reclaim, and the existing scheduler runtime.

Host test executables are generated in `build/tests/host/`, not stored as checked-in binaries. Run `make clean-host-test-artifacts` to remove them before a fresh host test pass.
