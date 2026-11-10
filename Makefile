CC := clang
LD := ld.lld
OBJCOPY := llvm-objcopy

CPPFLAGS := \
-Iarch/x86_64/idt \
-Iarch/x86_64/interrupts \
-Imm/physical \
-Imm/paging \
-Isched/core \
-Idrivers/serial

CFLAGS_32 := --target=i386-unknown-none -m32 \
-ffreestanding -fno-stack-protector -fno-pic -fno-pie \
-nostdlib -Wall -O2 -g -MMD -MP $(CPPFLAGS)

CFLAGS_64 := --target=x86_64-unknown-none -m64 \
-mno-red-zone -mno-sse -mno-mmx \
-ffreestanding -fno-stack-protector -fno-pic -fno-pie \
-nostdlib -Wall -O2 -g -MMD -MP $(CPPFLAGS)

LDFLAGS_32 := -m elf_i386 -T linker32.ld -nostdlib -static
LDFLAGS_64 := -m elf_x86_64 -T linker64.ld -nostdlib -static

OBJS_32 := boot/bios/boot.o

OBJS_64 := \
arch/x86_64/boot/entry64.o \
arch/x86_64/gdt/gdt_load.o \
arch/x86_64/cpu/ring3_entry.o \
kernel/core/main.o \
drivers/serial/serial.o \
arch/x86_64/idt/idt.o \
arch/x86_64/interrupts/exception.o \
arch/x86_64/interrupts/excep.o \
arch/x86_64/interrupts/pic.o \
arch/x86_64/interrupts/timer.o \
arch/x86_64/interrupts/irq.o \
arch/x86_64/interrupts/irq_entry.o \
mm/paging/paging.o \
mm/physical/memory.o \
sched/core/task.o \
arch/x86_64/cpu/switch.o

.PHONY: all run clean test-memory test-paging clean-memory-test test-boot

all: kernel.elf

kernel64.elf: $(OBJS_64) linker64.ld
	$(LD) $(LDFLAGS_64) -o $@ $(OBJS_64)

kernel64.bin: kernel64.elf
	$(OBJCOPY) -O binary $< $@

kernel64.o: kernel64.bin
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 $< $@

kernel.elf: $(OBJS_32) kernel64.o linker32.ld
	$(LD) $(LDFLAGS_32) -o $@ $(OBJS_32) kernel64.o

%.o: %.c
	$(CC) $(CFLAGS_64) -c $< -o $@

%.o: %.S
	$(CC) $(CFLAGS_64) -c $< -o $@

boot/bios/boot.o: boot/bios/boot.S
	$(CC) $(CFLAGS_32) -c $< -o $@

run: kernel.elf
	qemu-system-x86_64 -kernel kernel.elf -nographic -no-reboot

clean:
	rm -f $(OBJS_32) $(OBJS_64)
	rm -f $(OBJS_32:.o=.d) $(OBJS_64:.o=.d)
	rm -f kernel.elf kernel64.elf kernel64.bin kernel64.o
	rm -f $(HOST_MEMORY_TEST) $(HOST_PAGING_TEST)

HOST_CC ?= clang
HOST_TEST_CFLAGS := -std=c11 -Wall -Wextra -Werror
HOST_MEMORY_TEST := .memory-host-test
HOST_PAGING_TEST := .paging-host-test

test-memory: $(HOST_MEMORY_TEST)
	./$(HOST_MEMORY_TEST)

$(HOST_MEMORY_TEST): tests/memory_host_test.c mm/physical/memory.c mm/physical/memory.h
	$(HOST_CC) $(HOST_TEST_CFLAGS) -I. -Imm/physical -o $@ tests/memory_host_test.c mm/physical/memory.c

test-paging: $(HOST_PAGING_TEST)
	./$(HOST_PAGING_TEST)

$(HOST_PAGING_TEST): tests/paging_host_test.c mm/paging/paging.c mm/paging/paging.h
	$(HOST_CC) $(HOST_TEST_CFLAGS) -DVM_HOST_TEST -I. -Imm/paging -o $@ tests/paging_host_test.c mm/paging/paging.c

clean-memory-test:
	rm -f $(HOST_MEMORY_TEST) $(HOST_PAGING_TEST)

test-boot: kernel.elf kernel64.elf kernel64.bin
	bash tools/verify_boot.sh

-include $(OBJS_32:.o=.d)
-include $(OBJS_64:.o=.d)
