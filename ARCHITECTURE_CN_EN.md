# qmjyOS Architecture Guide / qmjyOS 系统架构详解

## 1. Architectural overview / 架构总览

### 中文

qmjyOS 当前采用“32 位引导层 + 64 位内核层”的两阶段结构。引导层负责满足 Multiboot v1 入口约定、准备最小运行环境、建立初始分页结构并进入 long mode；64 位内核层负责建立 C 运行环境、配置描述符表和中断基础设施、初始化内存管理，并启动 Ring 0 协作式任务。

需要区分三种不同的地址/内存概念：

1. **物理地址（Physical Address）**：内存芯片和物理页框所在的位置。
2. **虚拟地址（Virtual Address）**：CPU 指令访问时使用、由分页硬件翻译的地址。
3. **恒等映射（Identity Mapping）**：某个虚拟地址与同数值的物理地址对应。当前引导页表把前 1 GiB 这样映射。

启动阶段使用的引导页表与分页管理器维护的页表结构不是同一个东西。引导映射在早期启动阶段处于活动状态；Ring 3 运行流程会创建独立地址空间并通过 CR3 激活它。分页管理器自测试则独立检查页表数据结构。

### English

qmjyOS currently uses a two-stage structure: a 32-bit bootstrap layer followed by a 64-bit kernel layer. The bootstrap satisfies the Multiboot v1 entry contract, prepares a minimal execution environment, creates initial paging structures and enters long mode. The 64-bit kernel establishes the C execution environment, configures descriptor tables and interrupt infrastructure, initializes memory management, and starts cooperative Ring 0 tasks.

Three address/memory concepts must be distinguished:

1. **Physical address:** the location of a physical memory byte or frame.
2. **Virtual address:** the address used by CPU instructions and translated by paging hardware.
3. **Identity mapping:** a mapping where a virtual address has the same numeric value as its physical address. The current bootstrap maps the first 1 GiB this way.

The bootstrap page tables and the page-table manager are distinct. The bootstrap mapping is active during early boot. The Ring 3 runtime path creates and activates a separate address space through CR3; the paging manager self-test also checks page-table data structures independently.

## 2. Boot chain: Multiboot to 64-bit kernel / 启动链路：从 Multiboot 到 64 位内核

### 2.1 Multiboot header / Multiboot 头

`boot/bios/boot.S` emits a Multiboot v1 header in the `.multiboot` section. The header contains the magic value `0x1BADB002`, flags `0x2`, and a checksum calculated so that the header fields sum to zero modulo 32 bits. The linker script keeps this section near the beginning of the 32-bit image.

`boot/bios/boot.S` 在 `.multiboot` 段中生成 Multiboot v1 头。头部包含魔数 `0x1BADB002`、标志 `0x2`，以及使头部字段总和按 32 位运算结果为零的校验值。链接脚本通过 `KEEP` 保留该段，并将其放在 32 位映像靠前的位置。

### 2.2 32-bit entry and CPU setup / 32 位入口与 CPU 环境

The `_start` label is assembled in `.code32`. It disables maskable interrupts with `cli` and clears the direction flag with `cld`. It saves the Multiboot registers EAX and EBX into `boot_mb_magic` and `boot_mb_info`, because later setup operations reuse registers.

`_start` 标签以 `.code32` 汇编。入口先用 `cli` 关闭可屏蔽中断，用 `cld` 清除方向标志。随后把 Multiboot 传入的 EAX、EBX 保存到 `boot_mb_magic` 和 `boot_mb_info`，因为后续初始化会复用这些寄存器。

The bootstrap loads a temporary 32-bit GDT with `lgdt`, performs a far jump to selector `0x08` to reload CS, and loads data selectors `0x10` into DS, ES and SS. It then switches ESP to the bootstrap stack reserved in `.bss`.

引导代码通过 `lgdt` 加载临时 32 位 GDT，再远跳转到选择子 `0x08` 以刷新 CS，并把数据选择子 `0x10` 装入 DS、ES 和 SS。随后将 ESP 切换到 `.bss` 中预留的引导栈。

### 2.3 Initial page tables / 初始页表

Three page-aligned 4 KiB structures are reserved in `.bss`: `pml4`, `pdpt`, and `pd`. The bootstrap clears them, links PML4[0] to the PDPT, links PDPT[0] to the page directory, and fills 512 page-directory entries.

引导代码在 `.bss` 中预留三个按 4 KiB 对齐的结构：`pml4`、`pdpt` 和 `pd`。程序清零它们，让 PML4[0] 指向 PDPT、PDPT[0] 指向页目录，然后填充 512 个页目录项。

Each page-directory entry uses flags `0x83`: Present (bit 0), Read/Write (bit 1) and Page Size (bit 7). The physical base advances by `0x200000` (2 MiB) per entry. 512 entries therefore cover 512 × 2 MiB = 1 GiB. The upper paging levels are supervisor-only in this bootstrap mapping; the mapping is broad and not a user/kernel isolation layout.

每个页目录项使用标志 `0x83`：Present（第 0 位）、Read/Write（第 1 位）和 Page Size（第 7 位）。每项物理基址增加 `0x200000`（2 MiB）。512 项总共覆盖 512 × 2 MiB = 1 GiB。引导映射的上层页表项为 supervisor 权限；这是一种范围较大的早期映射，并不是用户/内核隔离布局。

### 2.4 Copying and entering the 64-bit image / 复制并进入 64 位映像

The 64-bit kernel is linked separately at 2 MiB (`linker64.ld`). The 32-bit bootstrap copies the embedded `kernel64.bin` payload to physical address `0x200000`. The outer 32-bit image contains the binary payload through the linker-generated `_binary_kernel64_bin_start` and `_binary_kernel64_bin_end` symbols.

64 位内核通过独立链接脚本 `linker64.ld` 链接到 2 MiB。32 位引导代码把嵌入的 `kernel64.bin` 复制到物理地址 `0x200000`。外层 32 位映像通过链接器生成的 `_binary_kernel64_bin_start` 和 `_binary_kernel64_bin_end` 符号访问这段二进制数据。

The bootstrap loads the physical address of PML4 into CR3, sets CR4.PAE, sets EFER.LME (bit 8) through MSR `0xC0000080`, and sets CR0.PG. It loads the 64-bit GDT and uses a far jump to selector `0x08` and offset `0x200000`. This transfers execution to the 64-bit entry code.

引导代码将 PML4 物理地址写入 CR3，设置 CR4.PAE，通过 MSR `0xC0000080` 设置 EFER.LME（第 8 位），并设置 CR0.PG。随后加载 64 位 GDT，并远跳转到选择子 `0x08`、偏移 `0x200000`，从而进入 64 位入口代码。

**Important:** the current bootstrap explicitly enables long mode and paging, but the inspected code does not show CPU NX capability detection or setting EFER.NXE. Therefore the documentation must not claim that NX page permissions are active merely because `paging.h` defines a `VM_NO_EXECUTE` flag.

**重要：** 当前引导代码明确开启了 long mode 和分页，但检查到的代码没有显示 CPU NX 能力检测，也没有设置 EFER.NXE。因此，不能因为 `paging.h` 定义了 `VM_NO_EXECUTE` 标志，就宣称 NX 页权限已经生效。

## 3. 64-bit entry and C environment / 64 位入口与 C 运行环境

`arch/x86_64/boot/entry64.S` receives the boot magic in EAX and the Multiboot information pointer in EBX. It copies them into R12D and R13D, disables interrupts, clears the direction flag, points RSP at `stack_top64`, aligns the stack to 16 bytes, and clears RBP.

`arch/x86_64/boot/entry64.S` 从 EAX 接收引导魔数，从 EBX 接收 Multiboot 信息指针。它把二者保存到 R12D 和 R13D，关闭中断、清除方向标志，将 RSP 指向 `stack_top64`，按 16 字节对齐栈，并清零 RBP。

It then clears the linker-defined BSS interval `[__bss_start, __bss_end)` using `rep stosb`, restores the boot arguments into the C calling-convention registers EDI and ESI, and calls `main`. If `main` returns, the entry code enters a `cli; hlt` loop.

随后它通过 `rep stosb` 清零链接器定义的 BSS 区间 `[__bss_start, __bss_end)`，把引导参数恢复到 C 调用约定使用的 EDI 和 ESI，然后调用 `main`。如果 `main` 返回，入口代码进入 `cli; hlt` 循环。

The Makefile compiles 64-bit code with `-ffreestanding`, `-nostdlib`, `-mno-red-zone`, `-mno-sse` and `-mno-mmx`. The no-red-zone option is important for a kernel that may receive interrupts on its current stack; the kernel cannot rely on the user-space red-zone convention.

Makefile 使用 `-ffreestanding`、`-nostdlib`、`-mno-red-zone`、`-mno-sse` 和 `-mno-mmx` 编译 64 位代码。对可能在当前栈上接收中断的内核而言，禁用 red zone 很重要；内核不能依赖用户态的 red-zone 约定。

## 4. GDT, selectors and TSS / GDT、选择子与 TSS

### 中文

GDT（Global Descriptor Table）为处理器提供段描述符。即使 64 位模式下大多数地址访问采用平坦模型，代码段属性和特权级仍然重要。当前 `main.c` 构造了以下描述符布局：

| GDT index | Selector | Meaning |
|---:|---:|---|
| 0 | `0x00` | Null descriptor / 空描述符 |
| 1 | `0x08` | Kernel code, DPL 0 / 内核代码段 |
| 2 | `0x10` | Kernel data, DPL 0 / 内核数据段 |
| 3 | `0x18` | User data descriptor, DPL 3 / 用户数据描述符 |
| 4 | `0x20` | User code descriptor, DPL 3 / 用户代码描述符 |
| 5–6 | `0x28` | 64-bit TSS descriptor, occupies 16 bytes / 64 位 TSS 描述符，占 16 字节 |

在用户态栈帧中，RPL 也要设置为 3，因此常见用户数据选择子为 `0x1B`，用户代码选择子为 `0x23`。TSS 选择子为 `0x28`。

TSS（Task State Segment）在 x86_64 中不再承担传统硬件任务切换的主要职责，但仍用于特权级切换时提供内核栈入口。当前代码设置 `tss.rsp0` 指向 `kernel_stack` 顶部，并设置 I/O bitmap base 超出 TSS limit，以表示没有可用 I/O bitmap。

`gdt_load.S` 执行 `lgdt`，再压入内核代码选择子 `0x08` 和重载标签地址，通过 `lretq` 刷新 CS。随后 C 代码执行 `ltr 0x28` 加载 TSS。这个 far return 是刷新 CS 的关键步骤，不应与普通 near `ret` 混淆。

### English

The Global Descriptor Table (GDT) supplies segment descriptors to the processor. Although most 64-bit address accesses use a flat model, code-segment attributes and privilege levels remain important. The current `main.c` constructs the following descriptor layout:

| GDT index | Selector | Meaning |
|---:|---:|---|
| 0 | `0x00` | Null descriptor |
| 1 | `0x08` | Kernel code, DPL 0 |
| 2 | `0x10` | Kernel data, DPL 0 |
| 3 | `0x18` | User data descriptor, DPL 3 |
| 4 | `0x20` | User code descriptor, DPL 3 |
| 5–6 | `0x28` | 64-bit TSS descriptor, occupying 16 bytes |

For a user-mode stack frame, the Requested Privilege Level (RPL) is also set to 3. The resulting user data selector is `0x1B`, and the user code selector is `0x23`. The TSS selector is `0x28`.

In x86_64, the Task State Segment (TSS) no longer primarily performs legacy hardware task switching. It remains important for supplying a kernel stack when a privilege-level transition occurs. The current code sets `tss.rsp0` to the top of `kernel_stack` and places the I/O bitmap base beyond the TSS limit, indicating that no usable I/O bitmap is present.

`gdt_load.S` executes `lgdt`, pushes kernel code selector `0x08` and a reload-label address, and uses `lretq` to refresh CS. C code then executes `ltr 0x28` to load the TSS. This far return is the key CS-reload step and must not be confused with an ordinary near `ret`.

## 5. IDT, exceptions and interrupt frames / IDT、异常与中断栈帧

### 中文

IDT（Interrupt Descriptor Table）包含 256 个门描述符。`idt.c` 负责构造表项并加载 IDTR；异常入口汇编和 IRQ 入口汇编提供 CPU 实际跳转到的代码地址。IDT 表项的代码段选择子必须指向有效的内核代码段，门类型和 DPL 决定哪些事件可以通过软件方式触发。

`excep.S` 为异常向量 0–31 提供入口桩。对于 CPU 自动压入错误码的异常，入口只压入向量号；对于不自动压入错误码的异常，入口先压入一个 0 作为统一占位，再压入向量号。这样公共处理路径就可以按固定顺序读取向量号和错误码。

公共异常入口应保存通用寄存器，保持栈布局稳定，再调用 C 处理函数。当前 `exception.c` 根据向量号输出异常名称、错误码和 RIP；对于 Page Fault（向量 14），还读取 CR2 输出故障线性地址，随后进入 `cli; hlt` 停机循环。

需要谨慎对待异常栈帧：CPU 是否额外压入旧 SS:RSP，取决于是否发生特权级切换；某些异常还会有 CPU 错误码。未来若允许 Ring 3，不能假设所有异常帧都只有 RIP、CS、RFLAGS，也不能在没有验证的情况下用固定偏移读取跨特权级字段。

### English

The Interrupt Descriptor Table (IDT) contains 256 gate descriptors. `idt.c` constructs the entries and loads IDTR; the exception and IRQ assembly files provide the code addresses to which the CPU transfers control. An IDT entry's code selector must refer to a valid kernel code segment. Gate type and DPL determine which events may be invoked through software.

`excep.S` provides entry stubs for exception vectors 0–31. For exceptions where the CPU pushes an error code, the stub pushes only the vector number. For exceptions without a CPU error code, the stub first pushes zero as a placeholder and then pushes the vector number. This allows the common path to read the vector and error code in a consistent order.

The common exception entry should save general-purpose registers, maintain a stable stack layout, and call the C handler. The current `exception.c` prints the exception name, error code and RIP. For Page Fault (vector 14), it also reads CR2 to report the faulting linear address, then enters a `cli; hlt` halt loop.

Exception frames require care: whether the CPU also pushes old SS:RSP depends on whether a privilege transition occurred; some exceptions also carry a CPU-pushed error code. If Ring 3 is enabled later, code must not assume that every frame contains only RIP, CS and RFLAGS, nor read cross-privilege fields using unverified fixed offsets.

## 6. PIC, PIT and IRQ flow / PIC、PIT 与 IRQ 流程

The current code remaps the legacy 8259 PIC so that master IRQs begin at vector 32 (`0x20`) and slave IRQs begin at vector 40 (`0x28`). It masks all slave IRQs and unmasks master IRQ0 and IRQ1. IRQ0 is the PIT timer; IRQ1 is the keyboard controller.

当前代码把传统 8259 PIC 重映射，使主片 IRQ 从向量 32（`0x20`）开始，从片 IRQ 从向量 40（`0x28`）开始。代码屏蔽所有从片 IRQ，只开放主片 IRQ0 和 IRQ1。IRQ0 对应 PIT 定时器，IRQ1 对应键盘控制器。

`timer.c` programs PIT channel 0 using the input clock of approximately 1,193,182 Hz. The divisor is derived from the requested frequency and clamped to the 16-bit counter range. The interrupt path increments a 64-bit tick counter.

`timer.c` 使用约 1,193,182 Hz 的输入时钟配置 PIT 通道 0。分频值由请求频率计算，并限制在 16 位计数器范围内。定时器中断路径会增加一个 64 位 tick 计数器。

`irq_entry.S` pushes a synthetic error-code slot and the remapped vector, saves general-purpose registers, aligns the stack before calling C, restores the original stack and registers, removes the two synthetic stack slots, and executes `iretq`. `irq.c` dispatches vector 32 to `timer_tick` and sends EOI; vector 33 currently sends EOI without reading keyboard data.

`irq_entry.S` 压入模拟错误码槽和重映射后的向量号，保存通用寄存器，在调用 C 函数前对齐栈，随后恢复原栈和寄存器，移除两个模拟栈槽并执行 `iretq`。`irq.c` 将向量 32 分发到 `timer_tick` 并发送 EOI；向量 33 当前只发送 EOI，并不读取键盘数据。

## 7. Physical memory allocator / 物理内存分配器

### 中文

`memory.c` 解析 Multiboot v1 信息结构中的内存映射字段。它检查 Multiboot magic、info flags、mmap 地址与长度，并按每条记录的 size 字段遍历变长条目。只有 type 1（可用内存）范围会进入候选管理区间。

分配器以 4 KiB 页为单位管理物理内存，使用位图记录页是否被占用。当前实现把管理上限限制为 1 GiB，因为引导阶段只建立了前 1 GiB 的恒等映射；这使早期分配器不会返回当前内核无法直接访问的高地址页框。

分配器会保留低端内存、内核映像以及 Multiboot 结构和调用方显式保留的区间。它区分“可管理页数”和“当前空闲页数”，并拒绝非法释放、重复释放，以及会覆盖仍在使用分配页的保留请求。初始化失败时会将分配器置为不可用状态，避免继续使用半初始化的数据。

### English

`memory.c` parses the memory-map fields in the Multiboot v1 information structure. It validates the Multiboot magic, information flags, map address and length, then walks variable-sized entries using each record's size field. Only type-1 (available RAM) ranges are considered candidates for management.

The allocator manages physical memory in 4 KiB pages and uses a bitmap to track page occupancy. Its current upper limit is 1 GiB because the bootstrap only creates an identity map for the first 1 GiB. This conservative limit prevents the early allocator from returning high physical frames that the current kernel cannot directly access through its bootstrap mapping.

The allocator reserves low memory, the kernel image, Multiboot structures and caller-specified ranges. It distinguishes the number of managed pages from the number of currently free pages. It rejects invalid frees, double frees, and reservation requests that would overlap a live allocation. If initialization fails, the allocator becomes unavailable rather than exposing partially initialized state.

## 8. Virtual memory manager / 虚拟内存管理器

### 中文

`paging.c` 管理一组静态的 4 KiB 页表页池。每张表有 512 个 64 位表项，对应 x86_64 四级分页中的一个层级。虚拟地址索引依次从 bit 39、30、21 和 12 取 9 位，分别索引 PML4、PDPT、PD 和 PT。

`vm_map_page` 检查地址规范性、4 KiB 对齐、物理地址范围和叶子标志，然后逐级查找或创建页表。创建过程中记录新表和权限提升记录；失败时回滚，以免留下部分建立的页表链。用户映射要求祖先页表项也具有 U/S 用户位，否则即使叶子 PTE 设置 U/S，CPU 仍会拒绝用户访问。

`vm_unmap_page` 清除叶子 PTE，并从下向上回收空页表页，但保留根表。`vm_query_page` 返回物理地址与叶子标志。`vm_self_test` 对未激活页表池执行映射、查询、重复映射、解除映射和用户权限路径等测试。

关键限制：`vm_init` 只创建独立根页表，不加载 CR3；页表页来自静态池，不由物理页分配器分配；模块注释明确要求这些表页仍处于当前恒等映射可访问的范围。因此当前模块是页表构建器/数据结构管理器，而不是完整的活动虚拟内存子系统。

### English

`paging.c` manages a static pool of 4 KiB page-table pages. Each table contains 512 64-bit entries, corresponding to one level of x86_64 four-level paging. The virtual-address indices are extracted as nine-bit fields at bit positions 39, 30, 21 and 12, selecting PML4, PDPT, PD and PT respectively.

`vm_map_page` validates canonical-address range, 4 KiB alignment, physical-address range and leaf flags, then walks or creates page-table levels. During creation it records newly allocated tables and ancestor-permission promotions; on failure it rolls these changes back so that a partially created hierarchy is not left behind. A user mapping requires the U/S user bit in every ancestor entry; setting U/S only in the leaf PTE is insufficient for user access.

`vm_unmap_page` clears the leaf PTE and reclaims empty page-table pages from the bottom upward while preserving the root. `vm_query_page` returns the physical address and leaf flags. `vm_self_test` exercises mapping, querying, duplicate mapping, unmapping and the user-permission path within the inactive page-table pool.

Critical limitation: `vm_init` creates a separate root but does not load CR3; page-table pages come from a static pool rather than the physical allocator; and the module comments require those pages to remain accessible through the current identity map. The current module is therefore a page-table builder/data-structure manager, not a complete active virtual-memory subsystem.

## 9. Ring 0 cooperative scheduler / Ring 0 协作式调度器

### 中文

`task.c` 定义两个演示任务 `task_a` 和 `task_b`。每个任务拥有独立栈和一个 `struct context`。初始化时，代码在任务栈上构造与 `switch.S` 弹栈顺序匹配的初始上下文，其中保存 R15、R14、R13、R12、RBX、RBP 和返回地址。

`switch.S` 的 `switch_to(old_ctx, new_ctx)` 先把当前任务的被调用者保存寄存器压栈，将当前 RSP 写入旧任务上下文指针，再把 RSP 切换到新任务上下文，恢复寄存器并 `ret` 到新任务上次停留的位置。新任务第一次启动时会进入 `task_trampoline`，由 R12 取出任务函数入口并调用。

`task_yield` 计算下一个任务、更新当前任务指针和切换计数，再调用 `switch_to`。这属于协作式调度：任务必须主动调用 `task_yield` 才会让出 CPU。如果任务永远不让出 CPU，其他任务不会因为 PIT tick 自动运行。

### English

`task.c` defines two demonstration tasks, `task_a` and `task_b`. Each task owns a separate stack and a `struct context`. During initialization, the code constructs an initial stack frame matching the pop order in `switch.S`, containing R15, R14, R13, R12, RBX, RBP and a return address.

`switch.S` implements `switch_to(old_ctx, new_ctx)`. It pushes the current task's callee-saved registers, stores the current RSP through the old-context pointer, switches RSP to the new context, restores the registers and returns to the point where the new task previously stopped. On first launch, a new task enters `task_trampoline`, which takes the task function address from R12 and calls it.

`task_yield` selects the next task, updates the current-task pointer and switch count, then calls `switch_to`. This is cooperative scheduling: a task must explicitly call `task_yield` to yield the CPU. If a task never yields, another task will not run merely because PIT ticks occur.

## 10. Main initialization order / 主流程初始化顺序

The current `main` sequence is important because it describes the actual integration:

当前 `main` 的顺序很重要，因为它体现了实际集成关系：

1. Initialize serial output and print the kernel banner.
   初始化串口并输出内核启动信息。
2. Initialize the physical allocator from Multiboot information and run its self-test if initialization succeeds.
   根据 Multiboot 信息初始化物理分配器；成功后运行自测试。
3. Run the paging manager self-test. The test operates on inactive page tables.
   运行分页管理器自测试；该测试操作的是未激活页表。
4. Remap the PIC, initialize the IDT and program the PIT.
   重映射 PIC、初始化 IDT 并配置 PIT。
5. Initialize GDT and TSS.
   初始化 GDT 和 TSS。
6. Print that the Ring 3 test is disabled because page permissions are not ready.
   输出 Ring 3 测试因页权限尚未准备好而禁用的信息。
7. Initialize the two-task cooperative scheduler.
   初始化双任务协作式调度器。
8. Execute `sti` and enter `task_start`.
   执行 `sti` 开启中断，并进入 `task_start`。

## 11. Security and correctness boundaries / 安全与正确性边界

- **No user isolation yet:** current bootstrap mappings are broad supervisor mappings; a future user address space needs deliberate kernel/user separation.
  **尚无用户隔离：** 当前引导映射是范围较大的 supervisor 映射；未来用户地址空间必须明确分离内核和用户区域。

- **NX is not proven active:** the current boot code does not show NX capability detection or EFER.NXE enablement.
  **NX 尚未证明生效：** 当前引导代码没有显示 NX 能力检测或 EFER.NXE 开启步骤。

- **Page-table manager is inactive:** no CR3 switch, TLB invalidation strategy for active mappings, or integration with physical-frame allocation is present in the documented interface.
  **页表管理器未激活：** 当前接口没有 CR3 切换、活动映射的 TLB 失效策略，也没有与物理页框分配集成。

- **No preemption:** the timer path does not switch task contexts.
  **没有抢占：** 定时器路径不会切换任务上下文。

- **Kernel panic is terminal:** exceptions halt the kernel.
  **内核 panic 是终止性的：** 异常发生后内核停机。

- **No complete syscall ABI:** a user-to-kernel service interface must be designed, validated and documented before Ring 3 can be considered useful.
  **没有完整系统调用 ABI：** 在 Ring 3 可被视为可用之前，必须设计、验证并记录用户到内核的服务接口。

## 12. Architecture glossary / 架构术语表

| Term | Meaning / 含义 |
|---|---|
| Multiboot v1 | Bootloader-to-kernel loading contract / 引导器与内核之间的加载约定 |
| GDT | Global Descriptor Table / 全局描述符表 |
| IDT | Interrupt Descriptor Table / 中断描述符表 |
| TSS | Task State Segment; supplies privilege-transition stack pointers in x86_64 / 任务状态段；在 x86_64 中提供特权级切换栈指针 |
| PIC | Programmable Interrupt Controller / 可编程中断控制器 |
| PIT | Programmable Interval Timer / 可编程间隔定时器 |
| IRQ | Hardware interrupt request / 硬件中断请求 |
| EOI | End Of Interrupt acknowledgement / 中断结束确认 |
| CR3 | Register containing the active top-level paging structure address / 保存活动顶层页表地址的寄存器 |
| PML4 | Top-level table in four-level x86_64 paging / x86_64 四级分页的顶层表 |
| PTE | Page-table entry / 页表项 |
| U/S | User/Supervisor permission bit / 用户态与内核态权限位 |
| NX | No-Execute page permission, subject to CPU support and EFER.NXE / 禁止执行页权限，依赖 CPU 支持和 EFER.NXE |
| Ring 0 | Most privileged x86 execution level / x86 最高特权级 |
| Ring 3 | Least privileged x86 execution level / x86 最低特权级 |
| Cooperative scheduling | Tasks yield explicitly / 任务主动让出 CPU 的调度方式 |
| Preemptive scheduling | Kernel can interrupt a running task and switch it / 内核可中断运行任务并切换的调度方式 |
