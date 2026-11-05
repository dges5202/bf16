# start.S: entry point of the bare-metal playground.
#
# Provides the minimum a C program expects from its runtime:
#   1. a stack pointer,
#   2. a zeroed .bss,
#   3. a call to main(), and
#   4. an exit(main()) fallback, since there is no operating system to
#      return to.
#
# linker.ld places _start at the load address. The program is linked without
# gp-relative relaxation, so gp is not initialized.

    .section .text._start
    .globl  _start
    .type   _start, @function

_start:
    # sp = top of the 4 KiB stack reserved by linker.ld.
    la      sp, __stack_top

    # Clear .bss one word at a time. linker.ld aligns both bounds to 4 bytes,
    # so every sw is naturally aligned. Addresses compare as unsigned.
    la      t0, __bss_start
    la      t1, __bss_end
1:  bgeu    t0, t1, 2f
    sw      zero, 0(t0)
    addi    t0, t0, 4
    j       1b

2:  # int main(void): its return value stays in a0 and becomes the status.
    call    main

    # exit(a0): the emulator's newlib-style system call 93.
    li      a7, 93
    ecall

    # exit does not return; park the hart if it ever does.
3:  j       3b

    .size   _start, .-_start
