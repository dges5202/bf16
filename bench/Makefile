CROSS   ?= riscv64-unknown-elf-
CC      := $(CROSS)gcc
AS      := $(CROSS)as
LD      := $(CROSS)ld
ARCH    := -march=rv32i_zicsr -mabi=ilp32
CFLAGS  := $(ARCH) -nostdlib -ffreestanding -fno-builtin -O2 -Wall -Wextra
ASFLAGS := $(ARCH)
LDFLAGS := -m elf32lriscv -T linker.ld --no-warn-rwx-segments
RV32EMU ?= ../../rv32emu/build/rv32emu

.PHONY: all c asm run-c run-asm clean

all: bench_c.elf bench_asm.elf

perfcounter.o: perfcounter.S
	$(AS) $(ASFLAGS) $< -o $@
start.o: start.S
	$(AS) $(ASFLAGS) $< -o $@
bf16_c_o2.o: bf16_c_o2.c
	$(CC) $(CFLAGS) -c $< -o $@
bench_c_main.o: bench_main.c
	$(CC) $(CFLAGS) -c $< -o $@
bench_asm_main.o: bench_main.c
	$(CC) $(CFLAGS) -DBENCH_ASM -c $< -o $@
bf16_asm.o: ../bf16.S
	$(AS) $(ASFLAGS) $< -o $@

bench_c.elf: start.o perfcounter.o bf16_c_o2.o bench_c_main.o linker.ld
	$(LD) $(LDFLAGS) -o $@ start.o perfcounter.o bf16_c_o2.o bench_c_main.o

bench_asm.elf: start.o perfcounter.o bf16_asm.o bench_asm_main.o linker.ld
	$(LD) $(LDFLAGS) -o $@ start.o perfcounter.o bf16_asm.o bench_asm_main.o

run-c: bench_c.elf
	$(RV32EMU) $<
run-asm: bench_asm.elf
	$(RV32EMU) $<
clean:
	$(RM) *.o *.elf

# --- Ripes 5ST microbench (fixed tiny corpus) ---
ripes_c_main.o: ripes_micro_main.c
	$(CC) $(CFLAGS) -c $< -o $@
ripes_asm_main.o: ripes_micro_main.c
	$(CC) $(CFLAGS) -DBENCH_ASM -c $< -o $@

ripes_c.elf: start.o bf16_c_o2.o ripes_c_main.o linker.ld
	$(LD) $(LDFLAGS) -o $@ start.o bf16_c_o2.o ripes_c_main.o

ripes_asm.elf: start.o bf16_asm.o ripes_asm_main.o linker.ld
	$(LD) $(LDFLAGS) -o $@ start.o bf16_asm.o ripes_asm_main.o

RIPES ?= /home/dges5202/Computer_architecture/tools/ripes-continuous/usr/bin/Ripes
# RV32_5S = 5-stage with forwarding + hazard detection
RIPES_PROC ?= RV32_5S
RIPES_FLAGS := --mode cli -t elf --proc $(RIPES_PROC) --timeout 120000 \
	--cycles --iret --cpi --ipc --runinfo --json

.PHONY: ripes-c ripes-asm ripes-c-ss
ripes-c: ripes_c.elf
	$(RIPES) $(RIPES_FLAGS) --src $<
ripes-asm: ripes_asm.elf
	$(RIPES) $(RIPES_FLAGS) --src $<
ripes-c-ss: ripes_c.elf
	$(RIPES) --mode cli -t elf --proc RV32_SS --timeout 60000 \
		--cycles --iret --cpi --ipc --runinfo --json --src $<

# --- soft-div loop: branched vs branchless ---
div_loop_variants.o: div_loop_variants.S
	$(AS) $(ASFLAGS) $< -o $@
div_loop_bench_main.o: div_loop_bench_main.c
	$(CC) $(CFLAGS) -c $< -o $@
div_loop_bench.elf: start.o perfcounter.o bf16_c_o2.o div_loop_variants.o div_loop_bench_main.o linker.ld
	$(LD) $(LDFLAGS) -o $@ start.o perfcounter.o bf16_c_o2.o div_loop_variants.o div_loop_bench_main.o

.PHONY: run-div-loop
run-div-loop: div_loop_bench.elf
	$(RV32EMU) $<

div_loop_ripes_br_main.o: div_loop_ripes_main.c
	$(CC) $(CFLAGS) -c $< -o $@
div_loop_ripes_nbr_main.o: div_loop_ripes_main.c
	$(CC) $(CFLAGS) -DUSE_NBR -c $< -o $@
div_loop_ripes_br.elf: start.o div_loop_variants.o div_loop_ripes_br_main.o linker.ld
	$(LD) $(LDFLAGS) -o $@ start.o div_loop_variants.o div_loop_ripes_br_main.o
div_loop_ripes_nbr.elf: start.o div_loop_variants.o div_loop_ripes_nbr_main.o linker.ld
	$(LD) $(LDFLAGS) -o $@ start.o div_loop_variants.o div_loop_ripes_nbr_main.o

.PHONY: ripes-div-loop
ripes-div-loop: div_loop_ripes_br.elf div_loop_ripes_nbr.elf
	$(RIPES) $(RIPES_FLAGS) --timeout 600000 --src div_loop_ripes_br.elf
	$(RIPES) $(RIPES_FLAGS) --timeout 600000 --src div_loop_ripes_nbr.elf

# --- full-domain classed instret (hot/nan/inf/zero/sub) ---
stats_class_c_main.o: stats_class_main.c
	$(CC) $(CFLAGS) -c $< -o $@
stats_class_asm_main.o: stats_class_main.c
	$(CC) $(CFLAGS) -DBENCH_ASM -c $< -o $@
stats_class_c.elf: start.o perfcounter.o bf16_c_o2.o stats_class_c_main.o linker.ld
	$(LD) $(LDFLAGS) -o $@ start.o perfcounter.o bf16_c_o2.o stats_class_c_main.o
stats_class_asm.elf: start.o perfcounter.o bf16_asm.o stats_class_asm_main.o linker.ld
	$(LD) $(LDFLAGS) -o $@ start.o perfcounter.o bf16_asm.o stats_class_asm_main.o

.PHONY: stats-class-c stats-class-asm stats-class
stats-class-c: stats_class_c.elf
	mkdir -p results
	$(RV32EMU) $< | tee results/stats_class_c_o2.txt
stats-class-asm: stats_class_asm.elf
	mkdir -p results
	$(RV32EMU) $< | tee results/stats_class_asm.txt
stats-class: stats-class-c stats-class-asm

# --- op filter: OPS=all (default) | add,sub | mul | div | ... ---
# Example while tuning add/sub only:
#   make stats-samp-asm OPS=add,sub
#   make path-class-asm OPS=add,sub
OPS ?= all
OPS_CFLAGS := $(shell python3 ops_cflags.py '$(OPS)')
OPS_SLUG := $(shell printf '%s' '$(OPS)' | tr ',' '_' )
ifeq ($(OPS),all)
  SAMP_OUT_ASM := results/stats_samp_asm.txt
  SAMP_OUT_C   := results/stats_samp_c_o2.txt
  PATH_OUT_ASM := results/path_class_asm.txt
  PATH_OUT_C   := results/path_class_c_o2.txt
else
  SAMP_OUT_ASM := results/stats_samp_asm_$(OPS_SLUG).txt
  SAMP_OUT_C   := results/stats_samp_c_o2_$(OPS_SLUG).txt
  PATH_OUT_ASM := results/path_class_asm_$(OPS_SLUG).txt
  PATH_OUT_C   := results/path_class_c_o2_$(OPS_SLUG).txt
endif

# --- fast hot-only sample (~65k hot pairs / op; per-opt KPI) ---
N_HOT ?= 65536
SAMP_CFLAGS := -DN_HOT=$(N_HOT) $(OPS_CFLAGS)
stats_samp_c_main.o: stats_samp_main.c
	$(CC) $(CFLAGS) $(SAMP_CFLAGS) -c $< -o $@
stats_samp_asm_main.o: stats_samp_main.c
	$(CC) $(CFLAGS) $(SAMP_CFLAGS) -DBENCH_ASM -c $< -o $@
stats_samp_c.elf: start.o perfcounter.o bf16_c_o2.o stats_samp_c_main.o linker.ld
	$(LD) $(LDFLAGS) -o $@ start.o perfcounter.o bf16_c_o2.o stats_samp_c_main.o
stats_samp_asm.elf: start.o perfcounter.o bf16_asm.o stats_samp_asm_main.o linker.ld
	$(LD) $(LDFLAGS) -o $@ start.o perfcounter.o bf16_asm.o stats_samp_asm_main.o

.PHONY: stats-samp-c stats-samp-asm stats-samp
# Rebuild mains every time so OPS=/N_HOT= changes are not stale.
stats-samp-c:
	$(RM) stats_samp_c_main.o
	$(MAKE) stats_samp_c.elf
	mkdir -p results
	$(RV32EMU) stats_samp_c.elf | tee $(SAMP_OUT_C)
stats-samp-asm:
	$(RM) stats_samp_asm_main.o
	$(MAKE) stats_samp_asm.elf
	mkdir -p results
	$(RV32EMU) stats_samp_asm.elf | tee $(SAMP_OUT_ASM)
stats-samp: stats-samp-c stats-samp-asm

# --- full-domain path-classed instret (coarse cls + fine path) ---
# Default skips u32 2^32 (use PATH_CLASS_CFLAGS= to include). Join: join_path_class.py
PATH_CLASS_CFLAGS ?= -DPATH_CLASS_SKIP_U32 $(OPS_CFLAGS)
path_class_c_main.o: path_class_main.c
	$(CC) $(CFLAGS) $(PATH_CLASS_CFLAGS) -c $< -o $@
path_class_asm_main.o: path_class_main.c
	$(CC) $(CFLAGS) $(PATH_CLASS_CFLAGS) -DBENCH_ASM -c $< -o $@
path_class_c.elf: start.o perfcounter.o bf16_c_o2.o path_class_c_main.o linker.ld
	$(LD) $(LDFLAGS) -o $@ start.o perfcounter.o bf16_c_o2.o path_class_c_main.o
path_class_asm.elf: start.o perfcounter.o bf16_asm.o path_class_asm_main.o linker.ld
	$(LD) $(LDFLAGS) -o $@ start.o perfcounter.o bf16_asm.o path_class_asm_main.o

.PHONY: path-class-c path-class-asm path-class path-class-quick path-class-join
path-class-c:
	$(RM) path_class_c_main.o
	$(MAKE) path_class_c.elf
	mkdir -p results
	$(RV32EMU) path_class_c.elf | tee $(PATH_OUT_C)
path-class-asm:
	$(RM) path_class_asm_main.o
	$(MAKE) path_class_asm.elf
	mkdir -p results
	$(RV32EMU) path_class_asm.elf | tee $(PATH_OUT_ASM)
path-class: path-class-asm path-class-c
path-class-join:
	python3 join_path_class.py $(PATH_OUT_ASM) $(PATH_OUT_C) results/path_class_join$(if $(filter-out all,$(OPS)),_$(OPS_SLUG),).md
path-class-quick:
	$(RM) path_class_asm_main.o path_class_c_main.o
	$(MAKE) path_class_asm_main.o path_class_c_main.o \
		PATH_CLASS_CFLAGS='-DPATH_CLASS_QUICK -DPATH_CLASS_SKIP_U32 $(OPS_CFLAGS)'
	$(MAKE) path_class_asm.elf path_class_c.elf
	mkdir -p results
	$(RV32EMU) path_class_asm.elf | tee results/path_class_asm_quick$(if $(filter-out all,$(OPS)),_$(OPS_SLUG),).txt
	$(RV32EMU) path_class_c.elf | tee results/path_class_c_o2_quick$(if $(filter-out all,$(OPS)),_$(OPS_SLUG),).txt
