# EduOS build (run inside WSL/Ubuntu or any Linux with gcc, binutils, nasm, qemu, python3).
# Optional cross toolchain:  make CROSS=i686-elf-
CROSS ?=
CC    := $(CROSS)gcc
LD    := $(CROSS)ld
OBJCOPY := $(CROSS)objcopy
NASM  := nasm
PY    := python3

BUILD := build
GEN   := $(BUILD)/gen
OBJ   := $(BUILD)/obj
CONF  := config/layout.conf

-include $(GEN)/layout.mk

KERNEL_C   := $(wildcard kernel/*.c) $(wildcard kernel/arch/x86/*.c)
KERNEL_ASM := $(wildcard kernel/arch/x86/*.asm)
KERNEL_OBJ := $(patsubst %.c,$(OBJ)/%.o,$(KERNEL_C)) $(patsubst %.asm,$(OBJ)/%.ao,$(KERNEL_ASM))

CFLAGS := -m32 -std=gnu11 -ffreestanding -fno-pie -fno-pic -fno-stack-protector -fno-builtin \
          -fno-asynchronous-unwind-tables -fno-unwind-tables -Wall -Wextra -Werror -O2 \
          -I kernel/include -I kernel/arch/x86 -I $(GEN)
ASFLAGS := -f elf32
LDFLAGS := -m elf_i386 -nostdlib -T kernel/linker.ld

KERNEL_ELF := $(BUILD)/kernel.elf
KERNEL_BIN := $(BUILD)/kernel.bin
STAGE1 := $(BUILD)/stage1.bin
STAGE2 := $(BUILD)/stage2.bin
IMAGE  := $(BUILD)/eduos.img
LOG    := $(BUILD)/eduos_events.log

.PHONY: all kernel boot image update-image run debug dashboard test check clean clean-image format-fs layout

all: boot kernel
	@echo "Build complete. First time? 'make image'. Otherwise 'make update-image' then 'make run'."

layout: $(GEN)/layout.mk
$(GEN)/layout.mk: $(CONF) scripts/gen_layout.py
	@mkdir -p $(GEN)
	$(PY) scripts/gen_layout.py layout $(CONF) $(GEN)

$(OBJ)/%.o: %.c $(GEN)/layout.mk
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ)/%.ao: %.asm $(GEN)/layout.mk
	@mkdir -p $(dir $@)
	$(NASM) $(ASFLAGS) $< -o $@

kernel: $(KERNEL_BIN)
$(KERNEL_ELF): $(KERNEL_OBJ) kernel/linker.ld
	$(LD) $(LDFLAGS) --defsym KERNEL_LOAD_ADDRESS=$(KERNEL_LOAD_ADDR) --defsym KERNEL_MAX_END=$(KERNEL_MAX_END) \
	    -o $@ $(KERNEL_OBJ)
$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@
	$(PY) scripts/gen_layout.py metadata $(CONF) $@ $(GEN)

boot: $(STAGE1) $(STAGE2)
$(STAGE1): boot/stage1.asm $(GEN)/layout.mk
	$(NASM) -f bin -I $(GEN)/ $< -o $@
	@test "$$(stat -c %s $@)" -eq 512 || (echo "stage1 must be exactly 512 bytes"; exit 1)
# Stage 2 embeds the kernel size, so it depends on the kernel binary.
$(STAGE2): boot/stage2.asm $(KERNEL_BIN)
	$(NASM) -f bin -I $(GEN)/ $< -o $@

image: all
	$(PY) scripts/mkimage.py create $(CONF) $(BUILD) $(if $(FORCE),--force,)

update-image: all
	$(PY) scripts/mkimage.py update $(CONF) $(BUILD)

format-fs:
	@echo "Formatting is done inside EduOS: boot it and run 'format confirm'."

run: all
	./scripts/run-qemu.sh
debug: all
	./scripts/debug-qemu.sh

dashboard:
	cd dashboard && npm install && npm start

check: all
	@file $(KERNEL_ELF) || true
	readelf -h $(KERNEL_ELF) | grep -E 'Class|Machine|Entry'
	nm $(KERNEL_ELF) | grep -E ' (_start|kernel_main|__kernel_start|__kernel_end)$$'
	@test -f $(IMAGE) && $(PY) scripts/verify-image.py $(CONF) $(BUILD) || echo "(no image yet)"

test:
	cd dashboard && npm test

clean:
	rm -rf $(OBJ) $(GEN) $(KERNEL_ELF) $(KERNEL_BIN) $(STAGE1) $(STAGE2)

clean-image:
	@read -p "Delete $(IMAGE) and ALL EduFS data? [y/N] " a; [ "$$a" = "y" ] && rm -f $(IMAGE) || echo "kept"
