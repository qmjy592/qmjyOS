# Host-side tests / 主机侧测试

The host-side tests exercise selected kernel subsystems without booting QEMU.

主机侧测试用于在不启动 QEMU 的情况下检查部分内核子系统。

## Physical memory allocator / 物理内存分配器

Run from the project root:

```sh
make test-memory
This builds tests/memory_host_test.c with mm/physical/memory.c.
The test checks Multiboot memory-map parsing, overlapping usable ranges,
reservations, allocation, freeing, double-free rejection, reinitialization,
malformed maps and rollback behavior.
该测试使用 tests/memory_host_test.c 和 mm/physical/memory.c，
检查 Multiboot 内存映射解析、可用区域重叠、保留区间、页面分配与释放、
重复释放拒绝、重新初始化、错误映射和回滚行为。
Paging manager / 分页管理器
Run:make test-paging
This runs the host-side paging manager self-test. It validates selected
inactive page-table data-structure operations; it does not prove CR3 switching,
hardware TLB behavior or complete address-space isolation.
该测试检查主机侧分页管理器的数据结构操作，但不能证明 CR3 切换、
硬件 TLB 行为或完整地址空间隔离正确。
Boot image / 启动映像
Run:make test-boot
This performs static checks on the generated boot images. It is not a
substitute for an actual BIOS or UEFI boot test in an emulator or on hardware.
该测试对生成的启动映像执行静态检查，不能替代在模拟器或真实硬件上的
BIOS/UEFI 实际启动测试。
Limitations / 限制
A host-side test may be unavailable on a host that disallows the requested
low-address memory mappings. A SKIP result is not a PASS.
如果主机不允许测试所需的低地址内存映射，主机测试可能显示 SKIP。
SKIP 不等于 PASS。
