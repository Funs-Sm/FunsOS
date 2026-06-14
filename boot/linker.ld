ENTRY(_start)

SECTIONS
{
    . = 0x100000;

    __kernel_start = .;

    .text : {
        *(.text)
        *(.text.*)
    }

    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }

    .data : {
        *(.data)
        *(.data.*)
    }

    .export_syms : {
        __export_syms_start = .;
        KEEP(*(.export_syms))
        __export_syms_end = .;
    }

    .bss : {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        __bss_end = .;
    }

    __kernel_end = .;
}
