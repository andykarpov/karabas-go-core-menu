/* The start of the menu program: the CPU leaves reset at 0 after the ROMs of boot.kg are loaded
   (menu_soc), with .data as loaded. Clear .bss, then main. */
    .section .text.start
    .global _start
_start:
    .option push
    .option norelax
    la      gp, __global_pointer$
    .option pop
    la      sp, __stack_top
    la      a0, __bss_start
    la      a1, __bss_end
1:  bgeu    a0, a1, 2f
    sw      zero, 0(a0)
    addi    a0, a0, 4
    j       1b
2:  call    main
3:  j       3b
