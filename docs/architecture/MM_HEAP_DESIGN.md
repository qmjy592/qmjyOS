# qmjyOS MM_HEAP — first implementation tranche

## Scope

This tranche introduces a bounded, statically reserved kernel heap for the x86_64 development baseline. It is intentionally an early allocator, not the final PMM/VMM-backed heap and not a slab allocator.

## Contract

- `qm_heap_init()` initializes the arena exactly once; a second initialization returns `QM_ERR_BUSY` and never invalidates live pointers.
- `kmalloc(n)` returns `NULL` for zero size, overflow, uninitialized heap or exhaustion.
- Successful allocations are aligned to 16 bytes.
- `kcalloc(count, size)` checks multiplication overflow and zeroes the returned range.
- `kfree(NULL)` succeeds. Foreign pointers and interior pointers are rejected. A second free is rejected with `QM_ERR_STATE`.
- Adjacent free blocks are coalesced, allowing reuse after fragmentation is removed.
- `qm_heap_get_stats()` reports arena size, free payload bytes, largest free block and block counts.

## Ownership and execution context

The allocator owns one 256 KiB `.bss` arena. It does not allocate or release physical pages. Returned storage is valid until `kfree()` or system shutdown. There is no lock, IRQ masking or preemption control inside the allocator; callers must serialize calls. It must not be used concurrently from task and interrupt contexts. This limitation is explicit until the kernel lock/preemption layer is implemented.

## Integration

QBuild maps `mm/heap/heap.c` into `MM_HEAP`, and `kernel/core/main.c` initializes it immediately after serial output setup and before filesystem/storage initialization. A failed initialization is logged; current heap-dependent consumers remain disabled because they are not yet connected. The module remains `experimental`: the implementation is built into the kernel, but it is a fixed arena, has no PMM/VMM growth path and has not been exercised in QEMU runtime. The host test covers 16-byte alignment, basic allocation, zeroing, invalid/double free, coalescing, exhaustion and recovery.

## Not claimed

This does not complete `MM_SLAB`, general virtual memory, multi-CPU safety, interrupt-safe allocation, DMA allocation or user-space memory allocation.
