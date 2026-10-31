ARCH ?= arm64

BUILD_DIR := build/$(ARCH)

ifeq ($(ARCH),arm64)
CROSS_COMPILE ?= aarch64-linux-gnu-
ARCH_DIR := arch/arm64
# the machine: qemu virt, or rpi4 (Raspberry Pi 4; qemu -machine raspi4b)
PLATFORM ?= virt
ifeq ($(PLATFORM),virt)
# qemu puts the DTB (1MB) at the start of RAM, 0x40000000, only if it
# fits below the lowest ELF segment
LOAD_ADDR := 0x40100000
else ifeq ($(PLATFORM),rpi4)
# RAM from 0: clear of the firmware's low pages, o2 at 0x200000 and
# qemu's DTB at 128MB
LOAD_ADDR := 0x1000000
BUILD_DIR := build/arm64-rpi4
ARCH_PLATFORM_OBJS := spintable.o
# qemu starts the raw image as the Pi firmware does, with its DTB; the
# machine has 4 CPUs and 2GB
DTB ?= $(BUILD_DIR)/bcm2711-rpi-4-b.dtb
QEMU_BOARD = -machine raspi4b -dtb $(DTB)
QEMU_KERNEL = $(BUILD_DIR)/h2.bin
RUN_NEEDS = $(DTB)
else
$(error Unsupported PLATFORM=$(PLATFORM). Use virt or rpi4)
endif
# h2 runs with its MMU off, where all memory is Device memory and an
# unaligned access faults: the compiler mustn't make any
ARCH_CFLAGS := -mgeneral-regs-only -mstrict-align -DPLATFORM_$(PLATFORM)
ARCH_LDFLAGS := --defsym=H2_LOAD_ADDR=$(LOAD_ADDR)
ARCH_OBJS := entry.o boot.o vectors.o trap.o psci.o stage2.o console.o guest.o \
	$(ARCH_PLATFORM_OBJS)
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

# qemu with h2 and whatever guest it was built with: virt, or raspi4b
CPUS ?= 3
MEM ?= 128M
QEMU_ARGS ?=
QEMU_BOARD ?= $(QEMU_MACHINE) -m $(MEM) -smp $(CPUS)
QEMU_KERNEL ?= $(BUILD_DIR)/h2.elf

run: all $(RUN_NEEDS)
	$(QEMU) $(QEMU_BOARD) -nographic -kernel $(QEMU_KERNEL) $(QEMU_ARGS)

# the Pi 4's device tree, from the firmware release o2 uses
$(BUILD_DIR)/bcm2711-rpi-4-b.dtb:
	@mkdir -p $(dir $@)
	curl -sSfL -o $@ https://github.com/raspberrypi/firmware/raw/1.20260915/boot/bcm2711-rpi-4-b.dtb

# o2 as the guest, with its disk (and network), as o2's own make qemu;
# o2 builds into the same build/<arch>[-<platform>] directories
O2 ?= ../o2
O2_BUILD ?= $(O2)/$(BUILD_DIR)
ifeq ($(PLATFORM),rpi4)
O2_DISK := sd.img
O2_QEMU_ARGS := -drive file=$(O2_BUILD)/sd.img,if=sd,format=raw
O2_RUN_ARGS := DTB=$(O2_BUILD)/bcm2711-rpi-4-b.dtb
else
O2_DISK := fs.img
O2_QEMU_ARGS := -global virtio-mmio.force-legacy=false \
	-drive file=$(O2_BUILD)/fs.img,if=none,format=raw,id=x0 \
	-device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0 \
	-netdev user,id=net0 -device virtio-net-device,netdev=net0,bus=virtio-mmio-bus.1
endif

run-o2:
	@test -f $(O2_BUILD)/kernel.elf -a -f $(O2_BUILD)/$(O2_DISK) || \
		{ echo "no o2 in $(O2_BUILD): make -C $(O2) ARCH=$(ARCH)" \
			"$(if $(filter rpi4,$(PLATFORM)),PLATFORM=rpi4)"; exit 1; }
	$(MAKE) run GUEST=$(O2_BUILD)/kernel.elf $(O2_RUN_ARGS) \
		QEMU_ARGS='$(O2_QEMU_ARGS) $(QEMU_ARGS)'

-include $(OBJS:.o=.d)
