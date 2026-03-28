/* Simplified vmlinux linker script for kbuild simulation */
ENTRY(_start)
SECTIONS {
    . = 0x40000000;
    .text : { *(.text) *(.text.*) }
    .rodata : { *(.rodata) *(.rodata.*) }
    . = ALIGN(4096);
    .data : { *(.data) *(.data.*) }
    .bss : { *(.bss) *(.bss.*) *(COMMON) }
    /DISCARD/ : { *(.comment) *(.note.*) }
}
