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
#include "qmjy/x86_64_trap.h"
#include "qmjy/syscall_x86_64.h"
#include "qmjy/process_runtime.h"
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
#define RING3_ELF_OFFSET 0x1000U
#define RING3_ELF_CAPACITY (RING3_ELF_OFFSET + MM_PAGE_SIZE)

extern void ring3_enter(uint64_t user_rsp, uint64_t user_rip);
extern char user_blob_start[];
extern char user_blob_end[];

struct ring3_elf64_header {
    uint8_t ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t phoff;
    uint64_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
} __attribute__((packed));

struct ring3_elf64_program_header {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t virtual_address;
    uint64_t physical_address;
    uint64_t file_size;
    uint64_t memory_size;
    uint64_t alignment;
} __attribute__((packed));

static uint8_t g_ring3_elf_file[RING3_ELF_CAPACITY]
    __attribute__((aligned(16)));

static size_t ring3_build_elf_image(void)
{
    struct ring3_elf64_header *header =
        (struct ring3_elf64_header *)(void *)g_ring3_elf_file;
    struct ring3_elf64_program_header *program;
    const size_t code_size = (size_t)(user_blob_end - user_blob_start);
    size_t i;

    if (code_size == 0 || code_size > MM_PAGE_SIZE ||
        RING3_ELF_OFFSET + code_size > sizeof(g_ring3_elf_file))
        return 0;

    for (i = 0; i < sizeof(g_ring3_elf_file); ++i)
        g_ring3_elf_file[i] = 0;

    header->ident[0] = 0x7F;
    header->ident[1] = 'E';
    header->ident[2] = 'L';
    header->ident[3] = 'F';
    header->ident[4] = 2; /* ELFCLASS64 */
    header->ident[5] = 1; /* little-endian */
    header->ident[6] = 1; /* current ELF version */
    header->type = 2;     /* ET_EXEC */
    header->machine = 62; /* EM_X86_64 */
    header->version = 1;
    header->entry = RING3_CODE_VA;
    header->phoff = sizeof(*header);
    header->ehsize = sizeof(*header);
    header->phentsize = sizeof(*program);
    header->phnum = 1;

    program = (struct ring3_elf64_program_header *)(void *)(
        g_ring3_elf_file + header->phoff);
    program->type = 1; /* PT_LOAD */
    program->flags = 4U | 1U; /* PF_R | PF_X; never writable+executable */
    program->offset = RING3_ELF_OFFSET;
    program->virtual_address = RING3_CODE_VA;
    program->physical_address = 0;
    program->file_size = code_size;
    program->memory_size = code_size;
    program->alignment = MM_PAGE_SIZE;

    for (i = 0; i < code_size; ++i)
        g_ring3_elf_file[RING3_ELF_OFFSET + i] =
            (uint8_t)user_blob_start[i];
    return RING3_ELF_OFFSET + code_size;
}

/*
 * The probe now follows the real ELF -> process -> private CR3 -> CPL3 path.
 * Ordinary syscalls round-trip through a saved trap frame and IRETQ. The
 * exit handler switches back to the kernel root before restoring the saved
 * Ring 0 continuation.
 */
static qm_syscall_result_t syscall_test_exit(
    const qm_syscall_context_t *context)
{
    if (context == NULL || context->number != QM_SYSCALL_TEST_EXIT)
        return QM_ERR_INVALID;
    if (qm_process_runtime_exit_current((qm_s32_t)context->arguments[0]) != QM_OK)
        return QM_ERR_STATE;
    serial_puts("[Ring 3] Process exit accepted; kernel CR3 restored.\n");
    return QM_OK;
}

static qm_syscall_result_t syscall_test_echo(
    const qm_syscall_context_t *context)
{
    if (context == NULL || context->number != QM_SYSCALL_TEST_ECHO)
        return QM_ERR_INVALID;
    return (qm_syscall_result_t)context->arguments[0];
}

qm_u64_t ring3_syscall_handler(qm_x86_64_trap_frame_t *frame)
{
    qm_syscall_context_t context = {0};
    qm_syscall_result_t result;
    qm_u64_t active_root;

    if (qm_syscall_context_from_x86_64_frame(frame, &context) != QM_OK)
        return (qm_u64_t)QM_ERR_PERMISSION;

    active_root = vm_current_root_physical();
    result = qm_syscall_dispatch(&context);
    if (active_root != vm_root_physical() &&
        vm_current_root_physical() == vm_root_physical())
        return QM_X86_64_TRAP_EXIT_TO_KERNEL;
    if (result < 0)
        serial_puts("[Ring 3] System call rejected by dispatcher.\n");
    return (qm_u64_t)result;
}

static int ring3_prepare_and_run(void)
{
    qm_process_handle_t process_id = 0;
    qm_process_runtime_info_t runtime_info;
    uint64_t physical = 0;
    uint64_t flags = 0;
    size_t elf_size;
    int result = -1;
    int process_loaded = 0;

    serial_puts("\n[Ring 3] Preparing ELF-backed isolated process.\n");
    if (!vm_nx_enabled()) {
        serial_puts("[Ring 3] ERROR: CPU NX support or EFER.NXE unavailable.\n");
        return -1;
    }
    if (vm_init() != VM_OK) {
        serial_puts("[Ring 3] ERROR: kernel page-table initialization failed.\n");
        return -1;
    }
    if (qm_process_runtime_init() != QM_OK) {
        serial_puts("[Ring 3] ERROR: process runtime initialization failed.\n");
        return -1;
    }

    elf_size = ring3_build_elf_image();
    if (elf_size == 0) {
        serial_puts("[Ring 3] ERROR: ELF smoke image construction failed.\n");
        return -1;
    }
    if (qm_process_runtime_load("ring3-elf-smoke", g_ring3_elf_file,
                                elf_size, &process_id) != QM_OK) {
        serial_puts("[Ring 3] ERROR: ELF-to-process load failed.\n");
        return -1;
    }
    process_loaded = 1;

    if (qm_process_runtime_query(process_id, &runtime_info) != QM_OK ||
        runtime_info.entry != RING3_CODE_VA ||
        runtime_info.owned_page_count != 2U ||
        runtime_info.user_stack_pointer == 0) {
        serial_puts("[Ring 3] ERROR: process runtime metadata invalid.\n");
        goto cleanup;
    }
    if (qm_process_runtime_query_page(process_id, RING3_CODE_VA,
                                      &physical, &flags) != QM_OK ||
        !(flags & VM_USER) || (flags & VM_WRITE) ||
        (flags & VM_NO_EXECUTE)) {
        serial_puts("[Ring 3] ERROR: ELF text mapping is not USER/RX.\n");
        goto cleanup;
    }
    if (qm_process_runtime_query_page(process_id,
            runtime_info.user_stack_pointer & ~(VM_PAGE_SIZE - 1U),
            &physical, &flags) != QM_OK ||
        !(flags & VM_USER) || !(flags & VM_WRITE) ||
        !(flags & VM_NO_EXECUTE)) {
        serial_puts("[Ring 3] ERROR: process stack mapping is not USER/RW/NX.\n");
        goto cleanup;
    }
    if (qm_process_runtime_query_page(process_id, 0x00200000ULL,
                                      &physical, &flags) != QM_OK ||
        (flags & VM_USER)) {
        serial_puts("[Ring 3] ERROR: shared kernel mapping is user-accessible.\n");
        goto cleanup;
    }

    serial_puts("[Ring 3] ELF loader committed private RX segment.\n");
    serial_puts("[Ring 3] Private PML4 and USER/RW/NX stack: ready.\n");
    serial_puts("[Ring 3] Kernel identity mapping remains supervisor-only.\n");

    if (qm_process_runtime_activate(process_id) != QM_OK) {
        serial_puts("[Ring 3] ERROR: private CR3 activation failed.\n");
        goto cleanup;
    }
    serial_puts("[Ring 3] Private CR3 activated. Entering CPL3.\n");
    ring3_enter(runtime_info.user_stack_pointer, runtime_info.entry);

    if (vm_current_root_physical() != vm_root_physical()) {
        serial_puts("[Ring 3] ERROR: returned without restoring kernel CR3.\n");
        goto cleanup;
    }
    if (qm_process_runtime_reap(process_id) != QM_OK) {
        serial_puts("[Ring 3] ERROR: exited process resource reclamation failed.\n");
        return -1;
    }
    process_loaded = 0;
    serial_puts("[Ring 3] ELF process exit and page reclamation: PASS.\n");
    result = 0;
    return result;

cleanup:
    if (process_loaded) {
        if (qm_process_runtime_terminate(process_id, -1) != QM_OK ||
            qm_process_runtime_reap(process_id) != QM_OK)
            serial_puts("[Ring 3] WARNING: failed process cleanup; resources retained.\n");
    }
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
    if (g_kernel_boot_info.firmware == QM_BOOT_FIRMWARE_BIOS)
        serial_dump_multiboot_map(g_boot_multiboot_magic, g_boot_multiboot_info_addr);
    else
        serial_puts("[BOOT] UEFI boot information is active.\n");
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
        g_kernel_boot_info.firmware == QM_BOOT_FIRMWARE_BIOS ?
            g_boot_multiboot_info_addr : 0,
        g_kernel_boot_info.firmware == QM_BOOT_FIRMWARE_BIOS ? 88U : 0,
        g_kernel_boot_info.firmware == QM_BOOT_FIRMWARE_BIOS ?
            g_boot_multiboot_mmap_addr : 0,
        g_kernel_boot_info.firmware == QM_BOOT_FIRMWARE_BIOS ?
            g_boot_multiboot_mmap_length : 0);
    if (status != MM_OK) {
        serial_puts("[MM] ERROR: physical page allocator initialization failed.\n");
        serial_put_hex32((uint32_t)status);
        serial_puts("\n");
        return QM_ERR_IO;
    }
    if (g_kernel_boot_info.firmware == QM_BOOT_FIRMWARE_BIOS)
        serial_puts("Physical page allocator: initialized from Multiboot mmap.\n");
    else
        serial_puts("Physical page allocator: initialized from qm_boot_info_t memory map.\n");
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
        serial_puts("[SYSCALL] ERROR: test-exit registration failed.\n");
        return status;
    }
    status = qm_syscall_register(QM_SYSCALL_TEST_ECHO, syscall_test_echo);
    if (status != QM_OK) {
        serial_puts("[SYSCALL] ERROR: test-echo registration failed.\n");
        return status;
    }
    serial_puts("[SYSCALL] Generic dispatcher initialized; Ring 3 ABI probes registered.\n");
    return QM_OK;
}

static qm_status_t kernel_init_storage(void)
{
    storage_init();
    return QM_OK; /* Storage is optional when no secondary QFS disk is attached. */
}

/* Shared post-handoff initialization for BIOS and UEFI. */
static void kernel_run_after_boot_info(void)
{
    qm_status_t status;
    qm_kernel_init_entry_t entries[] = {
        {"early-diagnostics", QM_KERNEL_INIT_EARLY, kernel_init_early},
        {"x86-architecture", QM_KERNEL_INIT_ARCH, kernel_init_arch},
        {"physical-memory-and-heap", QM_KERNEL_INIT_MEMORY, kernel_init_memory},
        {"vfs-and-syscall-core", QM_KERNEL_INIT_CORE, kernel_init_core},
        {"storage-filesystem", QM_KERNEL_INIT_FILESYSTEM, kernel_init_storage}
    };
    qm_size_t i;

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

    if (ring3_prepare_and_run() == 0)
        serial_puts("[Ring 3] Basic privilege transition: PASS.\n");
    else
        serial_puts("[Ring 3] Test did not complete successfully.\n");

    task_init();
    serial_puts("Starting cooperative Ring 0 scheduler.\n");
    __asm__ volatile("sti" ::: "memory");
    task_start();
}

/* BIOS compatibility entry: the existing Multiboot v1 path is preserved. */
void main(uint32_t multiboot_magic, uint32_t multiboot_info_addr)
{
    uint64_t kernel_base;
    uint64_t kernel_end;
    uint32_t mmap_addr;
    uint32_t mmap_length;
    qm_status_t status;

    serial_init();
    g_boot_multiboot_magic = multiboot_magic;
    g_boot_multiboot_info_addr = multiboot_info_addr;

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
    kernel_run_after_boot_info();
}

/* UEFI x64 entry: RDI points to a QBoot Protocol v1 structure. */
void main_uefi(qm_boot_info_t *boot_info)
{
    serial_init();
    g_boot_multiboot_magic = 0;
    g_boot_multiboot_info_addr = 0;
    g_boot_multiboot_mmap_addr = 0;
    g_boot_multiboot_mmap_length = 0;

    if (boot_info == NULL || qm_boot_validate_info(boot_info) != QM_OK ||
        boot_info->architecture != QM_BOOT_ARCH_X86_64 ||
        boot_info->firmware != QM_BOOT_FIRMWARE_UEFI) {
        serial_puts("[BOOT] ERROR: invalid UEFI qm_boot_info_t handoff.\n");
        for (;;) __asm__ volatile("cli; hlt" ::: "memory");
    }
    g_kernel_boot_info = *boot_info;
    serial_puts("[BOOT] UEFI qm_boot_info_t accepted.\n");
    kernel_run_after_boot_info();
}
