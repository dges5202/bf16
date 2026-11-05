CROSS   ?= riscv64-unknown-elf-
CC      := $(CROSS)gcc
AS      := $(CROSS)as
LD      := $(CROSS)ld

ARCH    := -march=rv32i -mabi=ilp32
CFLAGS  := $(ARCH) -nostdlib -ffreestanding -fno-builtin -O2 -Wall -Wextra
ASFLAGS := $(ARCH)
LDFLAGS := -m elf32lriscv -T linker.ld --no-warn-rwx-segments
RV32EMU ?= ../rv32emu/build/rv32emu

OBJS := start.o bf16_ref.o cross_main.o bf16_asm.o

.PHONY: all test test-full clean

all: add_cross.elf

bf16_asm.o: ../bf16.S
	$(AS) $(ASFLAGS) $< -o $@

start.o: start.S
	$(AS) $(ASFLAGS) $< -o $@

bf16_ref.o: bf16_ref.c
	$(CC) $(CFLAGS) -c $< -o $@

cross_main.o: cross_main.c
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -c $< -o $@

add_cross.elf: $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

test: EXTRA_CFLAGS :=
test: clean all
	$(RV32EMU) add_cross.elf; echo exit:$$?

test-full: EXTRA_CFLAGS := -DCROSS_FULL
test-full: clean all
	$(RV32EMU) add_cross.elf; echo exit:$$?

clean:
	$(RM) $(OBJS) add_cross.elf
