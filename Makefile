ARCH ?= arm64

BUILD_DIR := build/$(ARCH)

ifeq ($(ARCH),arm64)
CROSS_COMPILE ?= aarch64-linux-gnu-
ARCH_DIR := arch/arm64
ARCH_CFLAGS := -mgeneral-regs-only
ARCH_LDFLAGS :=
ARCH_OBJS := entry.o boot.o vectors.o trap.o psci.o console.o guest.o
else ifeq ($(ARCH),riscv64)
CROSS_COMPILE ?= riscv64-linux-gnu-
ARCH_DIR := arch/riscv64
ARCH_CFLAGS := -march=rv64imach_zicsr_zifencei -mabi=lp64 -mcmodel=medany
ARCH_LDFLAGS :=
ARCH_OBJS := entry.o boot.o trap_entry.o trap.o sbi.o console.o guest.o
else
$(error Unsupported ARCH=$(ARCH). Use arm64 or riscv64)
endif

CC := $(CROSS_COMPILE)gcc
LD := $(CROSS_COMPILE)ld
OBJCOPY := $(CROSS_COMPILE)objcopy

COMMON_CFLAGS := \
	-std=c11 -ffreestanding -fno-builtin -fno-stack-protector \
	-fno-pic -fno-pie -Wall -Wextra -Werror -O2 \
	-Iinclude -I$(ARCH_DIR)/include

COMMON_OBJS := hv.o fdt.o
OBJS := $(addprefix $(BUILD_DIR)/common/,$(COMMON_OBJS)) \
	$(addprefix $(BUILD_DIR)/arch/,$(ARCH_OBJS))

.PHONY: all clean
all: $(BUILD_DIR)/h2.elf $(BUILD_DIR)/h2.bin

$(BUILD_DIR)/h2.elf: $(OBJS) $(ARCH_DIR)/linker.ld
	$(LD) $(ARCH_LDFLAGS) -T $(ARCH_DIR)/linker.ld -o $@ $(OBJS)

$(BUILD_DIR)/h2.bin: $(BUILD_DIR)/h2.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD_DIR)/common/%.o: src/common/%.c
	mkdir -p $(dir $@)
	$(CC) $(COMMON_CFLAGS) $(ARCH_CFLAGS) -c $< -o $@

$(BUILD_DIR)/arch/%.o: $(ARCH_DIR)/%.c
	mkdir -p $(dir $@)
	$(CC) $(COMMON_CFLAGS) $(ARCH_CFLAGS) -c $< -o $@

$(BUILD_DIR)/arch/%.o: $(ARCH_DIR)/%.S
	mkdir -p $(dir $@)
	$(CC) $(COMMON_CFLAGS) $(ARCH_CFLAGS) -c $< -o $@

clean:
	rm -rf build
