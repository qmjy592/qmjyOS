CC := clang
LD := ld.lld
OBJCOPY := llvm-objcopy

CFLAGS_32 := --target=i386-unknown-none -m32 -ffreestanding -fno-stack-protector -fno-pic -fno-pie -nostdlib -Wall -O2 -g -Isrc
CFLAGS_64 := --target=x86_64-unknown-none -m64 -mno-red-zone -mno-sse -mno-mmx -ffreestanding -fno-stack-protector -fno-pic -fno-pie -nostdlib -Wall -O2 -g -Isrc

LDFLAGS_32 := -m elf_i386 -T linker32.ld -nostdlib -static
LDFLAGS_64 := -m elf_x86_64 -T linker64.ld -nostdlib -static

OBJS_32 := src/boot.o
OBJS_64 := src/entry64.o src/gdt_load.o src/main.o src/serial.o src/idt.o src/exception.o src/excep.o src/pic.o src/timer.o src/irq.o src/irq_entry.o src/paging.o src/memory.o src/task.o src/switch.o

all: kernel.elf

kernel64.elf: $(OBJS_64) linker64.ld
	$(LD) $(LDFLAGS_64) -o $@ $(OBJS_64)

kernel64.bin: kernel64.elf
	$(OBJCOPY) -O binary $< $@

kernel64.o: kernel64.bin
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 $< $@

kernel.elf: src/boot.o kernel64.o linker32.ld
	$(LD) $(LDFLAGS_32) -o $@ src/boot.o kernel64.o

%.o: %.c
	$(CC) $(CFLAGS_64) -c $< -o $@

src/boot.o: src/boot.S
	$(CC) $(CFLAGS_32) -c $< -o $@

src/excep.o: src/excep.S
	$(CC) $(CFLAGS_64) -c $< -o $@

src/irq_entry.o: src/irq_entry.S
	$(CC) $(CFLAGS_64) -c $< -o $@

src/switch.o: src/switch.S
	$(CC) $(CFLAGS_64) -c $< -o $@

run: kernel.elf
	qemu-system-x86_64 -kernel kernel.elf -nographic -no-reboot

src/gdt_load.o: src/gdt_load.S
	$(CC) $(CFLAGS_64) -c -o $@ $<
clean:
	rm -f src/*.o kernel.elf kernel64.elf kernel64.bin kernel64.o


src/entry64.o: src/entry64.S
	$(CC) $(CFLAGS_64) -c -o $@ $<

# Host-side physical memory allocator tests.
HOST_CC ?= clang
HOST_TEST_CFLAGS := -std=c11 -Wall -Wextra -Werror
HOST_MEMORY_TEST := .memory-host-test

.PHONY: test-memory
test-memory: $(HOST_MEMORY_TEST)
	./$(HOST_MEMORY_TEST)

$(HOST_MEMORY_TEST): tests/memory_host_test.c src/memory.c src/memory.h
	$(HOST_CC) $(HOST_TEST_CFLAGS) -I. -o $@ tests/memory_host_test.c src/memory.c

.PHONY: clean-memory-test
clean-memory-test:
	rm -f $(HOST_MEMORY_TEST)
