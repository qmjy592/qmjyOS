#include "serial.h"
#include "idt.h"
#include "pic.h"
#include "timer.h"
#include "task.h"
#include <stdint.h>

extern void gdt_load_and_reload_cs(const void *gdtr);

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

void main(void) {
    serial_init();
    serial_puts("Hello, x86_64 kernel!\n");

    pic_remap();
    idt_init();
    timer_init(100);

    init_gdt_and_tss();

    serial_puts("GDT and TSS initialized.\n");
    serial_puts("Ring 3 test temporarily disabled: page permissions are not ready.\n");
    serial_puts("Safe checkpoint: starting cooperative Ring 0 tasks.\n");
    __asm__ volatile("sti");

    uint64_t last_tick = 0;
    for (;;) {
        __asm__ volatile("hlt");
        uint64_t now = timer_get_ticks();
        if (now / 100 != last_tick / 100) {
            last_tick = now;
            serial_puts("Timer IRQ active: ticks=");
            // 暂时只输出验证标记，避免依赖尚未确认的数字格式化函数
            serial_puts("100-tick checkpoint\\n");
        }
    }
}
