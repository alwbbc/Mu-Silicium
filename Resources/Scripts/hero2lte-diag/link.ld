ENTRY(_start)
SECTIONS { . = 0; .text : { *(.text.head) *(.text*) *(.rodata*) } .data : { *(.data*) *(.bss*) *(COMMON) } _end = .; /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) } }
