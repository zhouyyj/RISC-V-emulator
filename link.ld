OUTPUT_ARCH( "riscv" )
ENTRY( _start )
MEMORY {
    RAM (rwx)   : ORIGIN = 0x200, LENGTH = 1M - 0x200
    IO  (rw)    : ORIGIN = 0x100000, LENGTH = 4K
}
SECTIONS {
    . = 0x200;
    /* text: test code section */
    .text : {
        *(.text)
        *(.text.*)
    }
    /* data: Initialized data segment */
    .data : {
        *(.data)
        *(.data.*)
    }
}
PROVIDE(__stack_top = 1M - 4K);
