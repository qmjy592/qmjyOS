CC := clang
LD := ld.lld
OBJCOPY := llvm-objcopy

HOST_CC ?= clang
HOST_TEST_CFLAGS := -std=c11 -Wall -Wextra -Werror

HOST_MEMORY_TEST := .memory-host-test
HOST_PAGING_TEST := .paging-host-test
HOST_VFS_TEST := .vfs-host-test
HOST_RAMFS_TEST := .ramfs-host-test
HOST_QFS_TEST := .qfs-host-test

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
-nostdlib -Wall -O2 -g -MMD -MP $(CPPFLAGS)

LDFLAGS_32 := -m elf_i386 -T linker32.ld -nostdlib -static
LDFLAGS_64 := -m elf_x86_64 -T linker64.ld -nostdlib -static

MODULE_MK := build/modules.mk

-include $(MODULE_MK)

.PHONY: all run clean \
        test-memory test-paging test-vfs test-ramfs test-qfs test-boot \
        clean-memory-test module-plan

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
	rm -f $(OBJS_32) $(OBJS_64)
	rm -f $(OBJS_32:.o=.d) $(OBJS_64:.o=.d)
	rm -f kernel.elf kernel64.elf kernel64.bin kernel64.o
	rm -f $(HOST_MEMORY_TEST) $(HOST_PAGING_TEST)
	rm -f $(HOST_VFS_TEST) $(HOST_RAMFS_TEST) $(HOST_QFS_TEST)
	rm -f $(HOST_INTERFACE_TEST)
	rm -f $(MODULE_MK)
	rm -rf build/qboot

test-memory: $(HOST_MEMORY_TEST)
	./$(HOST_MEMORY_TEST)

$(HOST_MEMORY_TEST): tests/memory_host_test.c mm/physical/memory.c mm/physical/memory.h
	$(HOST_CC) $(HOST_TEST_CFLAGS) -I. -Imm/physical -o $@ tests/memory_host_test.c mm/physical/memory.c

test-paging: $(HOST_PAGING_TEST)
	./$(HOST_PAGING_TEST)

$(HOST_PAGING_TEST): tests/paging_host_test.c mm/paging/paging.c mm/paging/paging.h
	$(HOST_CC) $(HOST_TEST_CFLAGS) -DVM_HOST_TEST -I. -Imm/paging -o $@ tests/paging_host_test.c mm/paging/paging.c

test-vfs: $(HOST_VFS_TEST)
	./$(HOST_VFS_TEST)

$(HOST_VFS_TEST): tests/filesystem/vfs_host_test.c fs/vfs/vfs.c fs/vfs/vfs.h
	$(HOST_CC) $(HOST_TEST_CFLAGS) -I. -o $@ tests/filesystem/vfs_host_test.c fs/vfs/vfs.c

test-ramfs: $(HOST_RAMFS_TEST)
	./$(HOST_RAMFS_TEST)

test-qfs: $(HOST_QFS_TEST)
	./$(HOST_QFS_TEST)

$(HOST_QFS_TEST): tests/filesystem/qfs_host_test.c fs/qfs/qfs.c fs/vfs/vfs.c drivers/block/block.c
	$(HOST_CC) $(HOST_TEST_CFLAGS) -I. -Iinclude -o $@ tests/filesystem/qfs_host_test.c fs/qfs/qfs.c fs/vfs/vfs.c drivers/block/block.c

$(HOST_RAMFS_TEST): tests/filesystem/ramfs_host_test.c fs/vfs/vfs.c fs/ramfs/ramfs.c fs/core/fs_init.c fs/qfs/qfs.c drivers/block/block.c
	$(HOST_CC) $(HOST_TEST_CFLAGS) -I. -Iinclude -o $@ tests/filesystem/ramfs_host_test.c fs/vfs/vfs.c fs/ramfs/ramfs.c fs/core/fs_init.c fs/qfs/qfs.c drivers/block/block.c

clean-memory-test:
	rm -f $(HOST_MEMORY_TEST) $(HOST_PAGING_TEST)
	rm -f $(HOST_VFS_TEST) $(HOST_RAMFS_TEST)
	rm -f $(HOST_INTERFACE_TEST)

test-boot: kernel.elf kernel64.elf kernel64.bin qboot-bios
	bash tools/verify_boot.sh
	bash tools/qboot/test_bios_boot.sh

-include $(OBJS_32:.o=.d)
-include $(OBJS_64:.o=.d)

# Public interface header compilation test.
HOST_INTERFACE_TEST := .interface-header-test

.PHONY: test-interfaces

test-interfaces: $(HOST_INTERFACE_TEST)
	./$(HOST_INTERFACE_TEST)

$(HOST_INTERFACE_TEST): tests/interfaces/interface_header_test.c
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
