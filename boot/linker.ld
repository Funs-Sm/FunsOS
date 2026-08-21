/* FunsOS kernel linker script (PE output).
 *
 * The kernel is built as a PE/COFF image so that the host mingw-w64
 * linker (which only supports PE out of the box) can produce it.
 *
 * The boot loader does NOT understand PE section headers.  It reads
 * kernel.bin (the objcopy -O binary output) linearly and writes each
 * byte at physical address 0x100000 + byte_offset.  Therefore every
 * section in the binary must be placed at byte_offset == VMA, i.e.
 * VMA == 0x100000 + cumulative_size_of_prior_sections.
 *
 * To achieve that with the PE backend we use a small trick: we define
 * ImageBase = 0x100000 and explicit section VMA offsets so that the
 * linker emits each section at the address the loader will write it
 * to.  The cumulative .text + .text.* size is `start_of_rodata`, and
 * we set VMA(.rodata) = 0x100000 + start_of_rodata, etc.  Because the
 * linker appends sections in declaration order, we can use the
 * location counter `.` BEFORE the section is declared to compute the
 * VMA of the section being declared.
 *
 * Each section also declares its proper alignment so .rdata stays 16
 * / 32-byte aligned and the loader's 4-byte copy loop is happy.
 */

ENTRY(_start)

SECTIONS
{
    . = 0x100000;

    __kernel_start = .;

    .text : {
        *(.text)
        *(.text.*)
    }

    /* Force .rodata to begin at .text end + alignment.  The location
     * counter advances naturally, so the resulting VMA is exactly
     * 0x100000 + sizeof(.text).  No PE ImageBase adjustment needed. */
    . = ALIGN(16);
    .rodata : {
        __export_syms_start = .;
        KEEP(*(.export_syms))
        __export_syms_end = .;
        *(.rodata)
        *(.rodata.*)
    }
    . = ALIGN(16);

    .data : {
        *(.data)
        *(.data.*)
    }
    . = ALIGN(16);

    .bss : {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        . = ALIGN(16);
        __bss_end = .;
    }

    __kernel_end = .;

    /DISCARD/ : {
        *(.note)
        *(.comment)
        *(.eh_frame)
    }
}
