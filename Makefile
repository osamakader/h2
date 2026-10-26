ARCH ?= arm64

BUILD_DIR := build/$(ARCH)

ifeq ($(ARCH),arm64)
CROSS_COMPILE ?= aarch64-linux-gnu-
ARCH_DIR := arch/arm64
ARCH_CFLAGS := -mgeneral-regs-only
ARCH_LDFLAGS :=
ARCH_OBJS := entry.o boot.o vectors.o trap.o psci.o stage2.o console.o guest.o
QEMU := qemu-system-aarch64
QEMU_MACHINE := -machine virt,virtualization=on,gic-version=3 -cpu cortex-a57
else ifeq ($(ARCH),riscv64)
CROSS_COMPILE ?= riscv64-linux-gnu-
ARCH_DIR := arch/riscv64
ARCH_CFLAGS := -march=rv64imach_zicsr_zifencei -mabi=lp64 -mcmodel=medany
ARCH_LDFLAGS :=
ARCH_OBJS := entry.o boot.o trap_entry.o trap.o sbi.o virq.o vplic.o stage2.o console.o guest.o
QEMU := qemu-system-riscv64
QEMU_MACHINE := -machine virt -cpu rv64,h=true -bios default
else
$(error Unsupported ARCH=$(ARCH). Use arm64 or riscv64)
endif

CC := $(CROSS_COMPILE)gcc
LD := $(CROSS_COMPILE)ld
OBJCOPY := $(CROSS_COMPILE)objcopy

COMMON_CFLAGS := \
	-std=c11 -ffreestanding -fno-builtin -fno-stack-protector \
	-fno-pic -fno-pie -Wall -Wextra -Werror -O2 -MMD -MP \
	-Iinclude -I$(ARCH_DIR)/include

COMMON_OBJS := hv.o cpu.o fdt.o elf.o stage2.o guest_image.o

# the guest: an ELF file to embed and load (default: the built-in guest)
GUEST ?=
GUEST_FILE := $(if $(GUEST),$(abspath $(GUEST)))
OBJS := $(addprefix $(BUILD_DIR)/common/,$(COMMON_OBJS)) \
	$(addprefix $(BUILD_DIR)/arch/,$(ARCH_OBJS))

.PHONY: all clean run run-o2 FORCE
all: $(BUILD_DIR)/h2.elf $(BUILD_DIR)/h2.bin

$(BUILD_DIR)/h2.elf: $(OBJS) $(ARCH_DIR)/linker.ld
	$(LD) $(ARCH_LDFLAGS) -T $(ARCH_DIR)/linker.ld -o $@ $(OBJS)

$(BUILD_DIR)/h2.bin: $(BUILD_DIR)/h2.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD_DIR)/common/%.o: src/common/%.c
	mkdir -p $(dir $@)
	$(CC) $(COMMON_CFLAGS) $(ARCH_CFLAGS) -c $< -o $@

$(BUILD_DIR)/common/%.o: src/common/%.S
	mkdir -p $(dir $@)
	$(CC) $(COMMON_CFLAGS) $(ARCH_CFLAGS) $(EXTRA_CFLAGS) -c $< -o $@

# the guest, stripped: only its segments matter. Rebuilt when the
# guest file, or which file it is, changes.
$(BUILD_DIR)/common/guest_image.o: $(if $(GUEST_FILE),$(BUILD_DIR)/guest.elf) $(BUILD_DIR)/guest
$(BUILD_DIR)/common/guest_image.o: EXTRA_CFLAGS := \
	$(if $(GUEST_FILE),-DHV_GUEST_IMAGE='"$(abspath $(BUILD_DIR))/guest.elf"')

$(BUILD_DIR)/guest.elf: $(GUEST_FILE) $(BUILD_DIR)/guest
	$(OBJCOPY) --strip-all $(GUEST_FILE) $@

$(BUILD_DIR)/guest: FORCE
	@mkdir -p $(dir $@)
	@echo '$(GUEST_FILE)' | cmp -s - $@ || echo '$(GUEST_FILE)' > $@

$(BUILD_DIR)/arch/%.o: $(ARCH_DIR)/%.c
	mkdir -p $(dir $@)
	$(CC) $(COMMON_CFLAGS) $(ARCH_CFLAGS) -c $< -o $@

$(BUILD_DIR)/arch/%.o: $(ARCH_DIR)/%.S
	mkdir -p $(dir $@)
	$(CC) $(COMMON_CFLAGS) $(ARCH_CFLAGS) -c $< -o $@

clean:
	rm -rf build

# qemu virt with h2 and whatever guest it was built with
CPUS ?= 3
MEM ?= 128M
QEMU_ARGS ?=

run: all
	$(QEMU) $(QEMU_MACHINE) -m $(MEM) -smp $(CPUS) -nographic \
		-kernel $(BUILD_DIR)/h2.elf $(QEMU_ARGS)

# o2 as the guest, with its disk and network, as o2's own make qemu
O2 ?= ../o2
O2_BUILD ?= $(O2)/build/$(ARCH)
O2_QEMU_ARGS := -global virtio-mmio.force-legacy=false \
	-drive file=$(O2_BUILD)/fs.img,if=none,format=raw,id=x0 \
	-device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0 \
	-netdev user,id=net0 -device virtio-net-device,netdev=net0,bus=virtio-mmio-bus.1

run-o2:
	@test -f $(O2_BUILD)/kernel.elf -a -f $(O2_BUILD)/fs.img || \
		{ echo "no o2 in $(O2_BUILD): make -C $(O2) ARCH=$(ARCH)"; exit 1; }
	$(MAKE) run GUEST=$(O2_BUILD)/kernel.elf \
		QEMU_ARGS='$(O2_QEMU_ARGS) $(QEMU_ARGS)'

-include $(OBJS:.o=.d)
