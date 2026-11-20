CC := clang
LD := ld.lld
OBJCOPY := llvm-objcopy

HOST_CC ?= clang
HOST_TEST_CFLAGS ?= -std=c11 -Wall -Wextra -Werror

HOST_TEST_BIN_DIR := build/tests/host
HOST_MEMORY_TEST := $(HOST_TEST_BIN_DIR)/memory-host-test
HOST_MEMORY_OVERLAP_TEST := build/tests/memory/memory_overlap_test
HOST_MEMORY_BOOT_INFO_TEST := build/tests/memory/memory_boot_info_test
HOST_PAGING_TEST := $(HOST_TEST_BIN_DIR)/paging-host-test
HOST_VFS_TEST := $(HOST_TEST_BIN_DIR)/vfs-host-test
HOST_RAMFS_TEST := $(HOST_TEST_BIN_DIR)/ramfs-host-test
HOST_QFS_TEST := $(HOST_TEST_BIN_DIR)/qfs-host-test

CPPFLAGS := \
-Iinclude \
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
-nostdlib -fno-builtin -Wall -O2 -g -MMD -MP $(CPPFLAGS)

LDFLAGS_32 := -m elf_i386 -T linker32.ld -nostdlib -static
LDFLAGS_64 := -m elf_x86_64 -z max-page-size=0x1000 -T linker64.ld -nostdlib -static

MODULE_MK := build/modules.mk

-include $(MODULE_MK)

.PHONY: all run clean \
        test-memory test-paging test-vfs test-ramfs test-qfs test-boot \
        test-kernel-foundation clean-memory-test clean-host-test-artifacts module-plan

all: kernel.elf

module-plan: $(MODULE_MK)
	@echo "模块构建计划：$(QBUILD_MODULE_PLAN)"

$(MODULE_MK): config/modules.conf config/modules.dep config/modules.status tools/qbuild/module_plan.py
	@mkdir -p build
	python3 tools/qbuild/module_plan.py --make

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
	rm -f .memory-host-test .paging-host-test .vfs-host-test .ramfs-host-test .qfs-host-test .interface-header-test .memory-overlap-test
	rm -f $(OBJS_32) $(OBJS_64)
	rm -f $(OBJS_32:.o=.d) $(OBJS_64:.o=.d)
	rm -f kernel.elf kernel64.elf kernel64-uefi.elf kernel64.bin kernel64.o
	rm -rf build/tests
	rm -f $(MODULE_MK)
	rm -rf build/qboot

$(HOST_TEST_BIN_DIR):
	mkdir -p $@

test-memory: $(HOST_MEMORY_TEST) $(HOST_MEMORY_OVERLAP_TEST) $(HOST_MEMORY_BOOT_INFO_TEST)
	./$(HOST_MEMORY_TEST)
	./$(HOST_MEMORY_OVERLAP_TEST)
	./$(HOST_MEMORY_BOOT_INFO_TEST)

$(HOST_MEMORY_TEST): tests/memory_host_test.c mm/physical/memory.c mm/physical/memory.h kernel/core/boot_info.c | $(HOST_TEST_BIN_DIR)
	$(HOST_CC) $(HOST_TEST_CFLAGS) -I. -Iinclude -Imm/physical -o $@ tests/memory_host_test.c mm/physical/memory.c kernel/core/boot_info.c

$(HOST_MEMORY_OVERLAP_TEST): tests/memory_overlap_test.c mm/physical/memory.c mm/physical/memory.h kernel/core/boot_info.c
	mkdir -p build/tests/memory
	$(HOST_CC) $(HOST_TEST_CFLAGS) -I. -Iinclude -Imm/physical -o $@ tests/memory_overlap_test.c mm/physical/memory.c kernel/core/boot_info.c

$(HOST_MEMORY_BOOT_INFO_TEST): tests/memory_boot_info_test.c mm/physical/memory.c mm/physical/memory.h kernel/core/boot_info.c
	mkdir -p build/tests/memory
	$(HOST_CC) $(HOST_TEST_CFLAGS) -I. -Iinclude -Imm/physical -o $@ tests/memory_boot_info_test.c mm/physical/memory.c kernel/core/boot_info.c

test-paging: $(HOST_PAGING_TEST)
	./$(HOST_PAGING_TEST)

$(HOST_PAGING_TEST): tests/paging_host_test.c mm/paging/paging.c mm/paging/paging.h | $(HOST_TEST_BIN_DIR)
	$(HOST_CC) $(HOST_TEST_CFLAGS) -DVM_HOST_TEST -I. -Imm/paging -o $@ tests/paging_host_test.c mm/paging/paging.c

test-vfs: $(HOST_VFS_TEST)
	./$(HOST_VFS_TEST)

$(HOST_VFS_TEST): tests/filesystem/vfs_host_test.c fs/vfs/vfs.c fs/vfs/vfs.h | $(HOST_TEST_BIN_DIR)
	$(HOST_CC) $(HOST_TEST_CFLAGS) -I. -o $@ tests/filesystem/vfs_host_test.c fs/vfs/vfs.c

test-ramfs: $(HOST_RAMFS_TEST)
	./$(HOST_RAMFS_TEST)

test-qfs: $(HOST_QFS_TEST)
	./$(HOST_QFS_TEST)

$(HOST_QFS_TEST): tests/filesystem/qfs_host_test.c fs/qfs/qfs.c fs/vfs/vfs.c drivers/block/block.c | $(HOST_TEST_BIN_DIR)
	$(HOST_CC) $(HOST_TEST_CFLAGS) -I. -Iinclude -o $@ tests/filesystem/qfs_host_test.c fs/qfs/qfs.c fs/vfs/vfs.c drivers/block/block.c

$(HOST_RAMFS_TEST): tests/filesystem/ramfs_host_test.c fs/vfs/vfs.c fs/ramfs/ramfs.c fs/core/fs_init.c fs/qfs/qfs.c drivers/block/block.c | $(HOST_TEST_BIN_DIR)
	$(HOST_CC) $(HOST_TEST_CFLAGS) -I. -Iinclude -o $@ tests/filesystem/ramfs_host_test.c fs/vfs/vfs.c fs/ramfs/ramfs.c fs/core/fs_init.c fs/qfs/qfs.c drivers/block/block.c

clean-memory-test:
	rm -rf $(HOST_TEST_BIN_DIR)

.PHONY: clean-host-test-artifacts
clean-host-test-artifacts:
	rm -rf build/tests
	rm -f .memory-host-test .paging-host-test .vfs-host-test .ramfs-host-test .qfs-host-test .interface-header-test .memory-overlap-test

test-boot: kernel.elf kernel64.elf kernel64.bin qboot-bios
	python3 tests/boot/test_phase3_runtime_markers.py
	python3 tests/boot/test_bios_nxe.py
	python3 tests/boot/test_qboot_gdt.py
	bash tools/verify_boot.sh
	bash tools/qboot/test_bios_boot.sh

	bash tools/qboot/test_qfs_ide.sh
-include $(OBJS_32:.o=.d)
-include $(OBJS_64:.o=.d)

# Public interface header compilation test.
HOST_INTERFACE_TEST := $(HOST_TEST_BIN_DIR)/interface-header-test

.PHONY: test-interfaces

test-interfaces: $(HOST_INTERFACE_TEST)
	./$(HOST_INTERFACE_TEST)

$(HOST_INTERFACE_TEST): tests/interfaces/interface_header_test.c | $(HOST_TEST_BIN_DIR)
	$(HOST_CC) $(HOST_TEST_CFLAGS) -Iinclude -o $@ $<

# ============================================================
# QMjyOS Foundation Services Host Tests
# ============================================================

.RECIPEPREFIX := >

HOST_QM_TEST_DIR := build/tests
HOST_QM_TEST_INCLUDES := -I. -Iinclude \
-Iservices/manager -Iservices/registry -Iservices/logging \
-Iservices/config -Iipc/message -Iinit/startup \
-Isecurity/capability

HOST_QM_TESTS := \
$(HOST_QM_TEST_DIR)/test_qm_service \
$(HOST_QM_TEST_DIR)/test_qm_ipc \
$(HOST_QM_TEST_DIR)/test_qm_registry \
$(HOST_QM_TEST_DIR)/test_qm_log \
$(HOST_QM_TEST_DIR)/test_qm_init \
$(HOST_QM_TEST_DIR)/test_qm_cap \
$(HOST_QM_TEST_DIR)/test_qm_service_config \
$(HOST_QM_TEST_DIR)/test_qm_system_integration

.PHONY: test-services clean-services

test-services: $(HOST_QM_TESTS)
>@set -e; for test in $(HOST_QM_TESTS); do echo "===== $$test ====="; ./$$test; done

$(HOST_QM_TEST_DIR):
>mkdir -p $@

$(HOST_QM_TEST_DIR)/test_qm_service: tests/services/test_qm_service.c services/manager/qm_service.c | $(HOST_QM_TEST_DIR)
>$(HOST_CC) $(HOST_TEST_CFLAGS) $(HOST_QM_TEST_INCLUDES) -o $@ $^

$(HOST_QM_TEST_DIR)/test_qm_ipc: tests/ipc/test_qm_ipc.c ipc/message/qm_ipc.c | $(HOST_QM_TEST_DIR)
>$(HOST_CC) $(HOST_TEST_CFLAGS) $(HOST_QM_TEST_INCLUDES) -o $@ $^

$(HOST_QM_TEST_DIR)/test_qm_registry: tests/services/test_qm_registry.c services/registry/qm_registry.c | $(HOST_QM_TEST_DIR)
>$(HOST_CC) $(HOST_TEST_CFLAGS) $(HOST_QM_TEST_INCLUDES) -o $@ $^

$(HOST_QM_TEST_DIR)/test_qm_log: tests/services/test_qm_log.c services/logging/qm_log.c | $(HOST_QM_TEST_DIR)
>$(HOST_CC) $(HOST_TEST_CFLAGS) $(HOST_QM_TEST_INCLUDES) -o $@ $^

$(HOST_QM_TEST_DIR)/test_qm_init: tests/services/test_qm_init.c init/startup/qm_init.c | $(HOST_QM_TEST_DIR)
>$(HOST_CC) $(HOST_TEST_CFLAGS) $(HOST_QM_TEST_INCLUDES) -o $@ $^

$(HOST_QM_TEST_DIR)/test_qm_cap: tests/security/test_qm_cap.c security/capability/qm_cap.c | $(HOST_QM_TEST_DIR)
>$(HOST_CC) $(HOST_TEST_CFLAGS) $(HOST_QM_TEST_INCLUDES) -o $@ $^

$(HOST_QM_TEST_DIR)/test_qm_service_config: tests/services/test_qm_service_config.c services/config/qm_service_config.c services/manager/qm_service.c | $(HOST_QM_TEST_DIR)
>$(HOST_CC) $(HOST_TEST_CFLAGS) $(HOST_QM_TEST_INCLUDES) -o $@ $^

$(HOST_QM_TEST_DIR)/test_qm_system_integration: tests/services/test_qm_system_integration.c services/manager/qm_service.c ipc/message/qm_ipc.c services/registry/qm_registry.c services/logging/qm_log.c init/startup/qm_init.c security/capability/qm_cap.c services/config/qm_service_config.c | $(HOST_QM_TEST_DIR)
>$(HOST_CC) $(HOST_TEST_CFLAGS) $(HOST_QM_TEST_INCLUDES) -o $@ $^

clean-services:
>rm -f $(HOST_QM_TESTS)

# QBoot BIOS disk image and real BIOS boot validation.
.PHONY: qboot-bios test-qboot-bios

qboot-bios: kernel64.bin
>bash tools/qboot/build_bios_image.sh

test-qboot-bios: qboot-bios
>bash tools/qboot/test_bios_boot.sh

# Independent QFS IDE persistence test.
.PHONY: test-qfs-ide

test-qfs-ide:
>bash tools/qboot/test_qfs_ide.sh


# Phase 2 kernel foundation host tests
HOST_KERNEL_FOUNDATION_DIR := build/tests/kernel-foundation
HOST_KERNEL_FOUNDATION_TESTS := \
$(HOST_KERNEL_FOUNDATION_DIR)/kernel-foundation \
$(HOST_KERNEL_FOUNDATION_DIR)/init-failure \
$(HOST_KERNEL_FOUNDATION_DIR)/syscall \
$(HOST_KERNEL_FOUNDATION_DIR)/boot-info-multiboot \
$(HOST_KERNEL_FOUNDATION_DIR)/boot-info-uefi

.PHONY: test-kernel-foundation
test-kernel-foundation: $(HOST_KERNEL_FOUNDATION_TESTS)
>@set -e; for test in $(HOST_KERNEL_FOUNDATION_TESTS); do echo "===== $$test ====="; ./$$test; done

$(HOST_KERNEL_FOUNDATION_DIR):
>mkdir -p $@

$(HOST_KERNEL_FOUNDATION_DIR)/kernel-foundation: tests/kernel/kernel_foundation_test.c kernel/core/boot_info.c kernel/core/init_registry.c | $(HOST_KERNEL_FOUNDATION_DIR)
>$(HOST_CC) $(HOST_TEST_CFLAGS) -Iinclude -o $@ $^

$(HOST_KERNEL_FOUNDATION_DIR)/init-failure: tests/kernel/init_failure_test.c kernel/core/boot_info.c kernel/core/init_registry.c | $(HOST_KERNEL_FOUNDATION_DIR)
>$(HOST_CC) $(HOST_TEST_CFLAGS) -Iinclude -o $@ $^

$(HOST_KERNEL_FOUNDATION_DIR)/syscall: tests/syscall/syscall_test.c kernel/syscall/syscall.c | $(HOST_KERNEL_FOUNDATION_DIR)
>$(HOST_CC) $(HOST_TEST_CFLAGS) -Iinclude -o $@ $^

$(HOST_KERNEL_FOUNDATION_DIR)/boot-info-multiboot: tests/kernel/boot_info_multiboot_test.c kernel/core/boot_info.c | $(HOST_KERNEL_FOUNDATION_DIR)
>$(HOST_CC) $(HOST_TEST_CFLAGS) -Iinclude -o $@ $^

$(HOST_KERNEL_FOUNDATION_DIR)/boot-info-uefi: tests/kernel/boot_info_uefi_test.c kernel/core/boot_info.c | $(HOST_KERNEL_FOUNDATION_DIR)
>$(HOST_CC) $(HOST_TEST_CFLAGS) -Iinclude -o $@ $^


# qmjyOS MM_HEAP host test
.PHONY: test-heap clean-heap-test
HOST_HEAP_TEST := build/tests/heap/heap_host_test

test-heap: $(HOST_HEAP_TEST)
>./$(HOST_HEAP_TEST)

$(HOST_HEAP_TEST): tests/memory/heap_host_test.c mm/heap/heap.c include/qmjy/heap.h | build/tests/heap
>$(HOST_CC) $(HOST_TEST_CFLAGS) -Iinclude -o $@ tests/memory/heap_host_test.c mm/heap/heap.c

build/tests/heap:
>mkdir -p $@

clean: clean-heap-test

clean-heap-test:
>rm -rf build/tests/heap


# Phase 2/3 ELF64 and process metadata host tests
HOST_PHASE23_TEST_DIR := build/tests/phase2-phase3
HOST_ELF64_TEST := $(HOST_PHASE23_TEST_DIR)/elf64-test
HOST_PROCESS_TEST := $(HOST_PHASE23_TEST_DIR)/process-test
HOST_PROCESS_RUNTIME_TEST := $(HOST_PHASE23_TEST_DIR)/process-runtime-test
HOST_SCHEDULER_ASPACE_TEST := $(HOST_PHASE23_TEST_DIR)/scheduler-aspace-test

.PHONY: test-elf64 test-process test-process-runtime test-scheduler-aspace
test-elf64: $(HOST_ELF64_TEST)
>./$(HOST_ELF64_TEST)

test-process: $(HOST_PROCESS_TEST)
>./$(HOST_PROCESS_TEST)

test-process-runtime: $(HOST_PROCESS_RUNTIME_TEST)
>./$(HOST_PROCESS_RUNTIME_TEST)

test-scheduler-aspace: $(HOST_SCHEDULER_ASPACE_TEST)
>./$(HOST_SCHEDULER_ASPACE_TEST)

$(HOST_PHASE23_TEST_DIR):
>mkdir -p $@

$(HOST_ELF64_TEST): tests/exec/elf64_test.c exec/elf/elf64.c include/qmjy/elf64.h | $(HOST_PHASE23_TEST_DIR)
>$(HOST_CC) $(HOST_TEST_CFLAGS) -Iinclude -o $@ tests/exec/elf64_test.c exec/elf/elf64.c

$(HOST_PROCESS_TEST): tests/kernel/process_test.c kernel/core/process.c include/qmjy/process.h | $(HOST_PHASE23_TEST_DIR)
>$(HOST_CC) $(HOST_TEST_CFLAGS) -Iinclude -o $@ tests/kernel/process_test.c kernel/core/process.c


# QBUILD-1.0.0-SELFTEST-TARGET
.RECIPEPREFIX := >
.PHONY: test-qbuild
test-qbuild:
>python3 tests/qbuild/test_qbuild_core.py


# QBoot x86_64 UEFI application and UEFI-entry kernel image.
UEFI_BUILD_DIR := build/qboot-uefi
UEFI_ESP_DIR := $(UEFI_BUILD_DIR)/esp
UEFI_CC ?= clang
UEFI_LD ?= lld-link
UEFI_TARGET := --target=x86_64-pc-windows-msvc
UEFI_CFLAGS := $(UEFI_TARGET) -std=c11 -O2 -Wall -Wextra -Werror \
    -ffreestanding -fshort-wchar -mno-red-zone \
    -fno-stack-protector -fno-builtin -nostdlib -Iinclude

.PHONY: qboot-uefi test-qboot-uefi

kernel64-uefi.elf: $(OBJS_64) linker64.ld
>$(LD) $(LDFLAGS_64) -e _start64_uefi -o $@ $(OBJS_64)

qboot-uefi: kernel64-uefi.elf
>mkdir -p $(UEFI_BUILD_DIR) $(UEFI_ESP_DIR)/EFI/BOOT $(UEFI_ESP_DIR)/EFI/QMJYOS
>$(UEFI_CC) $(UEFI_CFLAGS) -c boot/uefi/efi_main.c -o $(UEFI_BUILD_DIR)/efi_main.obj
>$(UEFI_CC) $(UEFI_CFLAGS) -c kernel/core/boot_info.c -o $(UEFI_BUILD_DIR)/boot_info.obj
>$(UEFI_LD) /machine:x64 /subsystem:efi_application /entry:efi_main /nodefaultlib \
>    /out:$(UEFI_ESP_DIR)/EFI/BOOT/BOOTX64.EFI \
>    $(UEFI_BUILD_DIR)/efi_main.obj $(UEFI_BUILD_DIR)/boot_info.obj
>cp kernel64-uefi.elf $(UEFI_ESP_DIR)/EFI/QMJYOS/KERNEL64-UEFI.ELF
>file $(UEFI_ESP_DIR)/EFI/BOOT/BOOTX64.EFI
>llvm-readobj --file-headers $(UEFI_ESP_DIR)/EFI/BOOT/BOOTX64.EFI
>llvm-readobj --file-headers --program-headers kernel64-uefi.elf

test-qboot-uefi: qboot-uefi
>python3 tests/boot/test_uefi_elf_layout.py kernel64-uefi.elf
>bash tools/qboot/test_uefi_boot.sh

$(HOST_PROCESS_RUNTIME_TEST): tests/kernel/process_runtime_test.c kernel/core/process_runtime.c kernel/core/user_access.c kernel/core/process.c exec/elf/elf64.c mm/paging/paging.c mm/heap/heap.c | $(HOST_PHASE23_TEST_DIR)
>$(HOST_CC) $(HOST_TEST_CFLAGS) -DVM_HOST_TEST -I. -Iinclude -Imm/paging -Imm/physical -o $@ tests/kernel/process_runtime_test.c kernel/core/process_runtime.c kernel/core/user_access.c kernel/core/process.c exec/elf/elf64.c mm/paging/paging.c mm/heap/heap.c

$(HOST_SCHEDULER_ASPACE_TEST): tests/kernel/scheduler_aspace_test.c sched/core/task.c mm/paging/paging.c | $(HOST_PHASE23_TEST_DIR)
>$(HOST_CC) $(HOST_TEST_CFLAGS) -DVM_HOST_TEST -I. -Iinclude -Imm/paging -Isched/core -Idrivers/serial -o $@ tests/kernel/scheduler_aspace_test.c sched/core/task.c mm/paging/paging.c


# x86_64 Ring 3 syscall trap-frame ABI tests.
.PHONY: test-syscall-x86_64-abi
SYSCALL_X86_64_ABI_TEST := build/tests/phase2-phase3/syscall-x86-64-abi-test

test-syscall-x86_64-abi: $(SYSCALL_X86_64_ABI_TEST)
>./$(SYSCALL_X86_64_ABI_TEST)

$(SYSCALL_X86_64_ABI_TEST): tests/syscall/x86_64_abi_test.c kernel/syscall/x86_64_abi.c | $(HOST_PHASE23_TEST_DIR)
>$(HOST_CC) $(HOST_TEST_CFLAGS) -Iinclude -o $@ $^
