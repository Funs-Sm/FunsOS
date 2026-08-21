/* user.ld - Linker script for FunsOS user-mode ELF executables
 *
 * Used by apps/Makefile to link apps/* into standalone ELF binaries that
 * can later be exec()'d by the kernel.  The script is intentionally
 * minimal: a single flat text+rodata segment, a writable data segment,
 * and a BSS.  The kernel's process_exec() does not honour a custom
 * program-header layout (it just maps all PT_LOAD segments), so the
 * addresses chosen here only need to be consistent across crt0.o and
 * the application's .o files.
 *
 * Layout:
 *   0x08048000  start of .text
 *   0x08048000+ ... .rodata, .data, .bss
 *   The user stack is allocated by the kernel in process_exec(); it does
 *   not live in the linked binary.
 */

OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
ENTRY(_start)

SECTIONS
{
    . = 0x08048000;

    .text : ALIGN(4) {
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
    }

    .data : ALIGN(4) {
        *(.data*)
        . = ALIGN(4);
    }

    .bss : ALIGN(4) {
        *(COMMON)
        *(.bss*)
        . = ALIGN(4);
    }

    /* The kernel's process_exec() reads e_entry from the ELF header; the
     * entry point is taken from the ENTRY directive above. */
}