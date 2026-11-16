#include "serial.h"
#include "idt.h"
#include "pic.h"
#include "timer.h"
#include "task.h"
#include "paging.h"
#include "memory.h"
#include "qmjy/heap.h"
#include "qmjy/kernel.h"
#include "qmjy/syscall.h"
#include "../../fs/core/fs_init.h"
#include "../../fs/qfs/qfs.h"
#include "../../fs/partition/mbr.h"
#include "../../drivers/block/ide_pio.h"
#include <stdint.h>

extern void gdt_load_and_reload_cs(const void *gdtr);

// 分配 16KB 内核栈，用于 Ring 3 中断时切换回来
uint8_t kernel_stack[16384] __attribute__((aligned(16)));
uint64_t kernel_stack_top;

// TSS 结构体 (64位)
struct tss_entry {
    uint32_t reserved0;
    uint64_t rsp0;       // 内核栈指针
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

struct tss_entry tss;

// GDT 表 (7个条目)
// 0: null
// 1: kernel code (0x08)
// 2: kernel data (0x10)
// 3: user data   (0x18)  <- 注意顺序
// 4: user code   (0x20)
// 5: TSS low     (0x28)
// 6: TSS high
uint64_t my_gdt[7];

void init_gdt_and_tss() {
    // 1. 初始化 TSS
    uint8_t *p = (uint8_t *)&tss;
    for (int i = 0; i < sizeof(tss); i++) p[i] = 0;

    kernel_stack_top = (uint64_t)kernel_stack + sizeof(kernel_stack);
    tss.rsp0 = kernel_stack_top; // 设置 Ring 3->0 切换时的内核栈
    tss.iomap_base = sizeof(tss);

    // 2. 填充 GDT
    my_gdt[0] = 0;
    // Kernel Code: 0x08
    my_gdt[1] = 0x00AF9A000000FFFF;
    // Kernel Data: 0x10
    my_gdt[2] = 0x00CF92000000FFFF;
    // User Data: 0x18 (DPL=3)
    my_gdt[3] = 0x00CFF2000000FFFF;
    // User Code: 0x20 (DPL=3, L=1)
    my_gdt[4] = 0x00AFFA000000FFFF;

    // 3. 构造 TSS 描述符 (类型必须为 0x9，即 0x89)
    uint64_t base = (uint64_t)&tss;
    uint64_t limit = sizeof(tss) - 1;
    uint64_t low = (limit & 0xFFFF) | ((base & 0xFFFFFF) << 16) |
                   (0x89ULL << 40) | (((limit >> 16) & 0xF) << 48) |
                   (((base >> 24) & 0xFF) << 56);
    uint64_t high = base >> 32;
    my_gdt[5] = low;
    my_gdt[6] = high;

    // 4. 加载 GDT 和 TSS
    struct { uint16_t limit; uint64_t base; } __attribute__((packed)) gdtr;
    gdtr.limit = 7 * 8 - 1;
    gdtr.base = (uint64_t)my_gdt;
    gdt_load_and_reload_cs(&gdtr);
    __asm__ volatile("ltr %0" :: "r"((uint16_t)0x28)); // TSS 选择子
}

// 用户态测试代码：尝试执行特权指令 cli
void user_code(void) {
    __asm__ volatile("cli");
    while(1);
}

// 从 Ring 0 跳转到 Ring 3
void enter_user_mode(void (*user_func)(void)) {
    static uint64_t user_stack[1024];
    uint64_t *sp = &user_stack[1024];
    sp = (uint64_t *)((uint64_t)sp & ~0xF); // 16字节对齐
    uint64_t user_rsp = (uint64_t)sp;

    // 严格按顺序压入 iretq 需要的 5 个值
    // SS = 0x18 | 3 = 0x1B (用户数据段，RPL=3)
    *(--sp) = 0x1B;
    // 先计算用户态 RSP，再压入，避免同一表达式中修改并读取 sp。
    *(--sp) = user_rsp;
    // RFLAGS = 0x202 (IF=1, bit1=1)
    *(--sp) = 0x202;
    // CS = 0x20 | 3 = 0x23 (用户代码段，RPL=3)
    *(--sp) = 0x23;
    // RIP = 用户函数入口
    *(--sp) = (uint64_t)user_func;

    __asm__ volatile(
        "mov %0, %%rsp\n"
        "iretq\n"
        :: "r"(sp) : "memory"
    );
}

extern char __kernel64_start, __kernel64_end;
extern void _start64(void);



/* -------------------------------------------------
 * Ring 3 isolated address space test
 * ------------------------------------------------- */

#define RING3_CODE_VA  0x0000000040000000ULL
#define RING3_STACK_VA 0x0000000040001000ULL

extern void ring3_enter(uint64_t user_rsp, uint64_t user_rip);
extern char user_blob_start[];
extern char user_blob_end[];

static void ring3_zero_page(uint64_t physical)
{
    volatile uint64_t *p = (volatile uint64_t *)(uintptr_t)physical;
    for (size_t i = 0; i < MM_PAGE_SIZE / sizeof(uint64_t); ++i)
        p[i] = 0;
}

/*
 * Called from syscall_entry on the TSS.RSP0 kernel stack.
 * RAX=1 is the Ring 3 test-exit request.
 */
/*
 * The only syscall exposed by the current Ring 3 probe is a test exit request.
 * It is deliberately routed through the generic dispatcher, but it is not a
 * production process-exit ABI and does not copy user pointers.
 */
static qm_syscall_result_t syscall_test_exit(
    const qm_syscall_context_t *context)
{
    if (context == NULL || context->number != QM_SYSCALL_TEST_EXIT)
        return QM_ERR_INVALID;
    serial_puts("[Ring 3] System call received successfully.\n");
    serial_puts("[Ring 3] Returning to Ring 0.\n");
    return QM_OK;
}

void ring3_syscall_handler(uint64_t syscall_number)
{
    qm_syscall_context_t context = {0};
    qm_syscall_result_t result;
    context.number = syscall_number;
    result = qm_syscall_dispatch(&context);
    if (result != QM_OK)
        serial_puts("[Ring 3] System call rejected by dispatcher.\n");
}

static int ring3_prepare_and_run(void)
{
    uint64_t code_pa = 0;
    uint64_t stack_pa = 0;
    uint64_t query_pa = 0;
    uint64_t query_flags = 0;
    int code_mapped = 0;
    int stack_mapped = 0;
    int activated = 0;
    int stack_page_releasable = 1;
    int code_page_releasable = 1;
    int result = -1;

    const size_t blob_size =
        (size_t)(user_blob_end - user_blob_start);

    serial_puts("\n[Ring 3] Preparing isolated user address space.\n");

    if (!vm_nx_enabled()) {
        serial_puts("[Ring 3] ERROR: CPU NX support or EFER.NXE unavailable.\n");
        return -1;
    }

    if (blob_size == 0 || blob_size > MM_PAGE_SIZE) {
        serial_puts("[Ring 3] ERROR: user code size invalid.\n");
        return -1;
    }

    /*
     * Rebuild clean inactive page tables after the paging self-test.
     */
    if (vm_init() != VM_OK) {
        serial_puts("[Ring 3] ERROR: vm_init failed.\n");
        return -1;
    }

    if (mm_alloc_page(&code_pa) != MM_OK) {
        serial_puts("[Ring 3] ERROR: cannot allocate user code page.\n");
        return -1;
    }

    if (mm_alloc_page(&stack_pa) != MM_OK) {
        serial_puts("[Ring 3] ERROR: cannot allocate user stack page.\n");
        mm_free_page(code_pa);
        return -1;
    }

    ring3_zero_page(code_pa);
    ring3_zero_page(stack_pa);

    /* Copy the position-independent user program to its own page. */
    {
        volatile uint8_t *dst = (volatile uint8_t *)(uintptr_t)code_pa;
        const uint8_t *src = (const uint8_t *)user_blob_start;

        for (size_t i = 0; i < blob_size; ++i)
            dst[i] = src[i];
    }

    /*
     * User code: USER, read-only, executable.
     * User stack: USER, writable, NX.
     */
    if (vm_map_page(RING3_CODE_VA, code_pa, VM_USER) != VM_OK) {
        serial_puts("[Ring 3] ERROR: code mapping failed.\n");
        goto cleanup;
    }
    code_mapped = 1;

    if (vm_map_page(RING3_STACK_VA, stack_pa,
                    VM_USER | VM_WRITE | VM_NO_EXECUTE) != VM_OK) {
        serial_puts("[Ring 3] ERROR: stack mapping failed.\n");
        goto cleanup;
    }
    stack_mapped = 1;

    /* Verify user code mapping. */
    if (vm_query_page(RING3_CODE_VA, &query_pa, &query_flags) != VM_OK ||
        query_pa != code_pa ||
        !(query_flags & VM_USER) ||
        (query_flags & VM_WRITE) ||
        (query_flags & VM_NO_EXECUTE)) {
        serial_puts("[Ring 3] ERROR: code permissions invalid.\n");
        goto cleanup;
    }

    /* Verify user stack mapping. */
    if (vm_query_page(RING3_STACK_VA, &query_pa, &query_flags) != VM_OK ||
        query_pa != stack_pa ||
        !(query_flags & VM_USER) ||
        !(query_flags & VM_WRITE) ||
        !(query_flags & VM_NO_EXECUTE)) {
        serial_puts("[Ring 3] ERROR: stack permissions invalid.\n");
        goto cleanup;
    }

    /* Verify the kernel identity mapping remains supervisor-only. */
    if (vm_query_page(0x00200000ULL, &query_pa, &query_flags) != VM_OK ||
        (query_flags & VM_USER)) {
        serial_puts("[Ring 3] ERROR: kernel mapping is user-accessible.\n");
        goto cleanup;
    }

    serial_puts("[Ring 3] User code page: RX, U/S=1.\n");
    serial_puts("[Ring 3] User stack page: RW/NX, U/S=1.\n");
    serial_puts("[Ring 3] Kernel identity mapping: supervisor-only.\n");

    /*
     * All mappings and permissions are ready.
     * vm_activate switches CR3 only after the kernel identity map exists.
     */
    if (vm_activate() != VM_OK) {
        serial_puts("[Ring 3] ERROR: CR3 activation failed.\n");
        goto cleanup;
    }
    activated = 1;

    serial_puts("[Ring 3] CR3 activated.\n");
    serial_puts("[Ring 3] Entering CPL3.\n");

    /*
     * The user program executes:
     *     mov $1, %eax
     *     int $0x80
     *
     * syscall_entry restores the saved Ring 0 stack and returns here.
     */
    ring3_enter(RING3_STACK_VA + MM_PAGE_SIZE - 16,
                RING3_CODE_VA);

    serial_puts("[Ring 3] Returned to Ring 0 successfully.\n");
    result = 0;

cleanup:
    /*
     * Once CR3 is active, vm_unmap_page invalidates the affected TLB entry.
     * Keep the active page-table root; the scheduler continues using it.
     */
    if (stack_mapped) {
        if (vm_unmap_page(RING3_STACK_VA) != VM_OK) {
            serial_puts("[Ring 3] WARNING: stack unmap failed.; page retained.\n");
            stack_page_releasable = 0;
            result = -1;
        }
    }

    if (code_mapped) {
        if (vm_unmap_page(RING3_CODE_VA) != VM_OK) {
            serial_puts("[Ring 3] WARNING: code unmap failed.; page retained.\n");
            code_page_releasable = 0;
            result = -1;
        }
    }

    if (stack_pa && stack_page_releasable &&
        mm_free_page(stack_pa) != MM_OK) {
        serial_puts("[Ring 3] WARNING: stack page release failed.\n");
        result = -1;
    }

    if (code_pa && code_page_releasable &&
        mm_free_page(code_pa) != MM_OK) {
        serial_puts("[Ring 3] WARNING: code page release failed.\n");
        result = -1;
    }

    (void)activated;
    return result;
}


static void serial_put_hex32(uint32_t value) {
    static const char digits[] = "0123456789ABCDEF";
    char out[11] = "0x00000000";
    for (int i = 0; i < 8; ++i) {
        out[9 - i] = digits[value & 0xFU];
        value >>= 4;
    }
    out[10] = '\0';
    serial_puts(out);
}

static void serial_dump_multiboot_map(uint32_t magic, uint32_t info_addr) {
    serial_puts("[MM DEBUG] magic=");
    serial_put_hex32(magic);
    serial_puts(" info=");
    serial_put_hex32(info_addr);
    serial_puts("\\n");

    if (!info_addr || (uint64_t)info_addr + 88 > (1ULL << 30)) {
        serial_puts("[MM DEBUG] info address outside mapped range.\\n");
        return;
    }

    const volatile uint32_t *info =
        (const volatile uint32_t *)(uintptr_t)info_addr;

    uint32_t flags = info[0];
    uint32_t mmap_length = info[11];
    uint32_t mmap_addr = info[12];

    serial_puts("[MM DEBUG] flags=");
    serial_put_hex32(flags);
    serial_puts(" mmap_addr=");
    serial_put_hex32(mmap_addr);
    serial_puts(" mmap_length=");
    serial_put_hex32(mmap_length);
    serial_puts("\\n");

    if (!(flags & (1U << 6)) || !mmap_addr || !mmap_length ||
        (uint64_t)mmap_addr + mmap_length > (1ULL << 30)) {
        serial_puts("[MM DEBUG] Multiboot mmap header invalid.\\n");
        return;
    }

    const volatile uint32_t *entry =
        (const volatile uint32_t *)(uintptr_t)mmap_addr;
    uint32_t count = mmap_length / 24;
    if (count > 4) count = 4;

    for (uint32_t i = 0; i < count; ++i) {
        const volatile uint32_t *e =
            (const volatile uint32_t *)(uintptr_t)(mmap_addr + i * 24);

        serial_puts("[MM DEBUG] entry ");
        serial_put_hex32(i);
        serial_puts(" size=");
        serial_put_hex32(e[0]);
        serial_puts(" base_hi=");
        serial_put_hex32(e[2]);
        serial_puts(" base_lo=");
        serial_put_hex32(e[1]);
        serial_puts(" len_hi=");
        serial_put_hex32(e[4]);
        serial_puts(" len_lo=");
        serial_put_hex32(e[3]);
        serial_puts(" type=");
        serial_put_hex32(e[5]);
        serial_puts("\\n");
    }
    (void)entry;
}


/* -------------------------------------------------
 * IDE + MBR + QFS storage initialization
 * Uses the secondary IDE device to avoid modifying
 * the QBoot system disk.
 * ------------------------------------------------- */

static void storage_init(void)
{
    qm_partition_t partitions[4];
    qm_u32_t partition_count = 0;
    qm_u32_t i;
    int result;

    serial_puts("\n[Storage] Initializing IDE block devices.\n");

    result = qm_ide_pio_init();

    if (result != 0) {
        serial_puts("[Storage] IDE initialization failed.\n");
        return;
    }

    serial_puts("[Storage] IDE controller initialized.\n");

    result = qm_mbr_scan(
        QM_IDE_PRIMARY_SLAVE,
        partitions,
        4,
        &partition_count
    );

    if (result != 0) {
        serial_puts("[Storage] Cannot read secondary disk MBR.\n");
        return;
    }

    if (partition_count == 0) {
        serial_puts("[Storage] No partition found on secondary disk.\n");
        return;
    }

    serial_puts("[Storage] MBR partition table detected.\n");

    for (i = 0; i < partition_count; ++i) {
        /*
         * Normal boot only mounts standard QFS partitions.
         * The dedicated QFS test partition type 0x7F is ignored.
         * Normal boot must never format a disk automatically.
         */
        if (partitions[i].type != 0x83U) {
            serial_puts("[Storage] Skipping non-QFS partition.\n");
            continue;
        }

        result = qfs_mount_at(&partitions[i], "/disk");

        if (result == VFS_OK) {
            serial_puts("[Storage] QFS mounted at /disk.\n");
            return;
        }
    }

    serial_puts("[Storage] No valid QFS partition found.\n");
    serial_puts("[Storage] Disk was NOT formatted or modified.\n");
}

/* QM_PHASE2_PHASE3_BOOT_INIT: ordered startup registry integration. */
static uint32_t g_boot_multiboot_magic;
static uint32_t g_boot_multiboot_info_addr;
static uint32_t g_boot_multiboot_mmap_addr;
static uint32_t g_boot_multiboot_mmap_length;
static qm_boot_info_t g_kernel_boot_info;
static qm_boot_memory_region_t g_kernel_memory_regions[QM_BOOT_MAX_MEMORY_REGIONS];

static qm_status_t kernel_init_early(void)
{
    serial_puts("Hello, x86_64 kernel!\n");
    serial_dump_multiboot_map(g_boot_multiboot_magic, g_boot_multiboot_info_addr);
    return QM_OK;
}

static qm_status_t kernel_init_arch(void)
{
    init_gdt_and_tss();
    pic_remap();
    idt_init();
    timer_init(100);
    serial_puts("GDT and TSS initialized.\n");
    return QM_OK;
}

static qm_status_t kernel_init_memory(void)
{
    int status;
    status = mm_init_from_boot_info(
        &g_kernel_boot_info,
        g_boot_multiboot_info_addr, 88U,
        g_boot_multiboot_mmap_addr, g_boot_multiboot_mmap_length);
    if (status != MM_OK) {
        serial_puts("[MM] ERROR: physical page allocator initialization failed.\n");
        serial_put_hex32((uint32_t)status);
        serial_puts("\n");
        return QM_ERR_IO;
    }
    serial_puts("Physical page allocator: initialized from Multiboot mmap.\n");
    if (mm_self_test() != MM_OK) {
        serial_puts("Physical page allocator self-test: FAIL.\n");
        return QM_ERR_STATE;
    }
    serial_puts("Physical page allocator self-test: PASS (alloc/free/double-free).\n");

    if (qm_heap_init() != QM_OK) {
        serial_puts("[MM] ERROR: bounded kernel heap initialization failed.\n");
        return QM_ERR_NO_MEMORY;
    }
    serial_puts("[MM] Kernel heap initialized (bounded early arena).\n");

    if (vm_self_test() != 0) {
        serial_puts("Paging manager self-test: FAIL.\n");
        return QM_ERR_STATE;
    }
    serial_puts("Paging manager self-test: PASS (inactive page tables).\n");
    return QM_OK;
}

static qm_status_t kernel_init_core(void)
{
    qm_status_t status;
    if (fs_init() != 0) {
        serial_puts("[FS] ERROR: filesystem initialization failed.\n");
        return QM_ERR_IO;
    }
    serial_puts("[FS] VFS initialized.\n");
    serial_puts("[FS] RAMFS mounted as root filesystem.\n");

    status = qm_syscall_register(QM_SYSCALL_TEST_EXIT, syscall_test_exit);
    if (status != QM_OK) {
        serial_puts("[SYSCALL] ERROR: test syscall registration failed.\n");
        return status;
    }
    serial_puts("[SYSCALL] Generic dispatcher initialized; Ring 3 test-exit registered.\n");
    return QM_OK;
}

static qm_status_t kernel_init_storage(void)
{
    storage_init();
    return QM_OK; /* Storage is optional when no secondary QFS disk is attached. */
}

/* QM_PHASE2_PHASE3_MAIN: Multiboot compatibility adapter + ordered init. */
void main(uint32_t multiboot_magic, uint32_t multiboot_info_addr)
{
    uint64_t kernel_base;
    uint64_t kernel_end;
    uint32_t mmap_addr;
    uint32_t mmap_length;
    qm_status_t status;
    qm_kernel_init_entry_t entries[] = {
        {"early-diagnostics", QM_KERNEL_INIT_EARLY, kernel_init_early},
        {"x86-architecture", QM_KERNEL_INIT_ARCH, kernel_init_arch},
        {"physical-memory-and-heap", QM_KERNEL_INIT_MEMORY, kernel_init_memory},
        {"vfs-and-syscall-core", QM_KERNEL_INIT_CORE, kernel_init_core},
        {"storage-filesystem", QM_KERNEL_INIT_FILESYSTEM, kernel_init_storage}
    };
    qm_size_t i;

    serial_init();
    g_boot_multiboot_magic = multiboot_magic;
    g_boot_multiboot_info_addr = multiboot_info_addr;

    /* The current bootstrap identity-maps only the first GiB at this stage. */
    if (multiboot_info_addr == 0 ||
        (uint64_t)multiboot_info_addr + 88U > (1ULL << 30)) {
        serial_puts("[BOOT] ERROR: Multiboot info pointer is outside the early identity map.\n");
        for (;;) __asm__ volatile("cli; hlt" ::: "memory");
    }
    mmap_length = ((const volatile uint32_t *)(uintptr_t)multiboot_info_addr)[11];
    mmap_addr = ((const volatile uint32_t *)(uintptr_t)multiboot_info_addr)[12];
    g_boot_multiboot_mmap_addr = mmap_addr;
    g_boot_multiboot_mmap_length = mmap_length;
    kernel_base = (uint64_t)(uintptr_t)&__kernel64_start;
    kernel_end = (uint64_t)(uintptr_t)&__kernel64_end;
    if (kernel_end <= kernel_base || mmap_addr == 0 || mmap_length == 0) {
        serial_puts("[BOOT] ERROR: kernel or Multiboot memory-map range is invalid.\n");
        for (;;) __asm__ volatile("cli; hlt" ::: "memory");
    }

    status = qm_boot_info_from_multiboot_v1(
        multiboot_magic,
        (const void *)(uintptr_t)multiboot_info_addr,
        multiboot_info_addr,
        (const void *)(uintptr_t)mmap_addr,
        mmap_addr,
        g_kernel_memory_regions,
        (uint64_t)(uintptr_t)g_kernel_memory_regions,
        QM_BOOT_MAX_MEMORY_REGIONS,
        kernel_base,
        kernel_end - kernel_base,
        kernel_base,
        (uint64_t)(uintptr_t)&_start64,
        &g_kernel_boot_info);
    if (status != QM_OK) {
        serial_puts("[BOOT] ERROR: Multiboot v1 to qm_boot_info_t conversion failed.\n");
        serial_put_hex32((uint32_t)status);
        serial_puts("\n");
        for (;;) __asm__ volatile("cli; hlt" ::: "memory");
    }
    serial_puts("[BOOT] Versioned qm_boot_info_t constructed and validated.\n");

    for (i = 0; i < sizeof(entries) / sizeof(entries[0]); ++i) {
        status = qm_kernel_init_register(&entries[i]);
        if (status != QM_OK) {
            serial_puts("[KERNEL] ERROR: init callback registration failed.\n");
            serial_put_hex32((uint32_t)status);
            serial_puts("\n");
            for (;;) __asm__ volatile("cli; hlt" ::: "memory");
        }
    }

    status = qm_kernel_init(&g_kernel_boot_info);
    if (status != QM_OK) {
        serial_puts("[KERNEL] ERROR: ordered initialization registry failed.\n");
        serial_put_hex32((uint32_t)status);
        serial_puts("\n");
        for (;;) __asm__ volatile("cli; hlt" ::: "memory");
    }
    serial_puts("[KERNEL] Ordered initialization registry: PASS.\n");

    /* Preserve the existing isolated Ring 3 smoke test and cooperative scheduler. */
    if (ring3_prepare_and_run() == 0) {
        serial_puts("[Ring 3] Basic privilege transition: PASS.\n");
    } else {
        serial_puts("[Ring 3] Test did not complete successfully.\n");
    }

    task_init();
    serial_puts("Starting cooperative Ring 0 scheduler.\n");
    __asm__ volatile("sti" ::: "memory");
    task_start();
}
