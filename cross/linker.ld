/*
 * Linker script for the bare-metal playground.
 *
 * The image is a flat RV32 program placed at 0x10000, with a 4 KiB stack
 * above .bss. start.S reads the symbols exported here:
 *
 *   __bss_start, __bss_end   4-byte aligned bounds of the region to clear
 *   __stack_top              initial sp, 16-byte aligned
 *
 * .rodata holds the constant tables. The small-data sections (.srodata,
 * .sdata, .sbss) and COMMON are unused today but listed explicitly, so that
 * a later change cannot leave a zero-initialized object outside the cleared
 * .bss region or in an unpredictable position.
 */
OUTPUT_ARCH("riscv")

ENTRY(_start)

SECTIONS
{
    . = 0x10000;

    /* Entry code first, so that _start sits at the load address. */
    .text : {
        *(.text._start)
        *(.text .text.*)
    }

    .rodata : {
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
    }

    .data : {
        *(.data .data.*)
        *(.sdata .sdata.*)
    }

    .bss : {
        . = ALIGN(4);
        __bss_start = .;
        *(.sbss .sbss.*)
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    }

    /* Not part of the file image; ld only reserves the address range. */
    .stack (NOLOAD) : {
        . = ALIGN(16);
        . += 16384;
        __stack_top = .;
    }
}
