#!/data/data/com.termux/files/usr/bin/bash
set -euo pipefail

cd "$HOME/qmjyOS"

echo "===== qmjyOS 模块化迁移 ====="

# 源文件与目标文件映射
declare -a SOURCES=(
"src/boot.S|boot/bios/boot.S"
"src/entry64.S|arch/x86_64/boot/entry64.S"
"src/gdt_load.S|arch/x86_64/gdt/gdt_load.S"
"src/ring3_entry.S|arch/x86_64/cpu/ring3_entry.S"
"src/switch.S|arch/x86_64/cpu/switch.S"
"src/idt.c|arch/x86_64/idt/idt.c"
"src/idt.h|arch/x86_64/idt/idt.h"
"src/excep.S|arch/x86_64/interrupts/excep.S"
"src/exception.c|arch/x86_64/interrupts/exception.c"
"src/irq.S|arch/x86_64/interrupts/irq.S"
"src/irq.c|arch/x86_64/interrupts/irq.c"
"src/irq.h|arch/x86_64/interrupts/irq.h"
"src/irq_entry.S|arch/x86_64/interrupts/irq_entry.S"
"src/pic.c|arch/x86_64/interrupts/pic.c"
"src/pic.h|arch/x86_64/interrupts/pic.h"
"src/timer.c|arch/x86_64/interrupts/timer.c"
"src/timer.h|arch/x86_64/interrupts/timer.h"
"src/memory.c|mm/physical/memory.c"
"src/memory.h|mm/physical/memory.h"
"src/paging.c|mm/paging/paging.c"
"src/paging.h|mm/paging/paging.h"
"src/task.c|sched/core/task.c"
"src/task.h|sched/core/task.h"
"src/serial.c|drivers/serial/serial.c"
"src/serial.h|drivers/serial/serial.h"
"src/main.c|kernel/core/main.c"
)

# 第一步：全部预检查
echo "[1/5] 检查源文件和目标路径"

for item in "${SOURCES[@]}"; do
    src="${item%%|*}"
    dst="${item##*|}"

    if [ ! -f "$src" ]; then
        echo "[ERROR] 源文件不存在：$src"
        exit 1
    fi

    if [ -e "$dst" ]; then
        echo "[ERROR] 目标文件已经存在：$dst"
        echo "为防止覆盖，迁移已停止。"
        exit 1
    fi
done

echo "[OK] 全部源文件和目标路径检查通过"

# 第二步：备份
echo "[2/5] 创建项目备份"

BACKUP_ROOT="$HOME/qmjyOS-migration-backups"
mkdir -p "$BACKUP_ROOT"

STAMP="$(date +%Y%m%d-%H%M%S)"
BACKUP="$BACKUP_ROOT/qmjyOS-before-$STAMP.tar.gz"

tar -czf "$BACKUP" \
    src Makefile linker32.ld linker64.ld \
    tools tests scripts docs

echo "[OK] 备份完成：$BACKUP"

# 第三步：迁移源码
echo "[3/5] 移动源码"

for item in "${SOURCES[@]}"; do
    src="${item%%|*}"
    dst="${item##*|}"

    mkdir -p "$(dirname "$dst")"
    mv "$src" "$dst"

    echo "[MOVE] $src -> $dst"
done

# 第四步：生成新版 Makefile
echo "[4/5] 更新 Makefile"

cat > Makefile <<'MAKEFILE'
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
MAKEFILE

echo "[OK] 新 Makefile 已生成"

# 第五步：检查旧目录，不删除历史文件
echo "[5/5] 检查旧 src 目录"

echo "旧目录剩余内容："
find src -maxdepth 1 -type f 2>/dev/null | sort || true

echo
echo "===== 迁移操作完成 ====="
echo "备份：$BACKUP"
echo "旧 src 目录及历史文件暂时保留。"
