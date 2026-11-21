/* The menu program in the SRAM (menu_soc): program, data, heap and stack below the frame buffers
   (FB0 0x300000, FB1 0x380000), MMIO at 0x80000000. */
OUTPUT_ARCH("riscv")
ENTRY(_start)

MEMORY { RAM (rwx) : ORIGIN = 0x00000000, LENGTH = 0x300000 }

STACK_SIZE = 64K;

SECTIONS
{
    .text : {
        KEEP(*(.text.start))
        *(.text .text.*)
    } > RAM
    .rodata : ALIGN(8) {
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
    } > RAM
    .data : ALIGN(8) {
        __data_start = .;
        *(.data .data.*)
        __global_pointer$ = . + 0x800;
        *(.sdata .sdata.*)
        . = ALIGN(8);
        __data_end = .;
    } > RAM
    .bss (NOLOAD) : ALIGN(8) {
        __bss_start = .;
        *(.sbss .sbss.*)
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(8);
        __bss_end = .;
    } > RAM
    __heap_start = .;
    __stack_top = ORIGIN(RAM) + LENGTH(RAM);
    __heap_end = __stack_top - STACK_SIZE;
}
