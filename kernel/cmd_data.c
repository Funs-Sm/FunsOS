/*
 * kernel/cmd_data.c - P5: Data utility commands
 * dd, split, join, hexdump, strings, cksum
 */

#include "cmd_data.h"
#include "shell.h"
#include "shell_error.h"
#include "string.h"
#include "stdio.h"

void cmd_dd(const char *args) {
    if (!args || !*args) {
        shell_print("dd - convert and copy a file\n");
        shell_print("Usage: dd [OPERAND]...\n");
        shell_print("  if=FILE       Input file (default: stdin)\n");
        shell_print("  of=FILE       Output file (default: stdout)\n");
        shell_print("  bs=BYTES      Block size in bytes (default: 512)\n");
        shell_print("  count=N       Copy only N input blocks\n");
        shell_print("  skip=N        Skip N input blocks at start\n");
        shell_print("  seek=N        Skip N output blocks at start\n");
        shell_print("  conv=MODE     Conversion: notrunc | sync | sparse\n");
        shell_print("\nExamples:\n");
        shell_print("  dd if=/dev/zero of=disk.img bs=1M count=16\n");
        shell_print("  dd if=input.bin of=output.bin bs=4096\n");
        shell_last_exit_code = 1;
        return;
    }
    shell_print("dd: stub (operand parsing not yet implemented)\n");
    shell_print("  requested: ");
    shell_print(args);
    shell_print("\n");
    shell_last_exit_code = 0;
}

void cmd_split(const char *args) {
    if (!args || !*args) {
        shell_print("split - split a file into pieces\n");
        shell_print("Usage: split [OPTION] [FILE [PREFIX]]\n");
        shell_print("  -b SIZE      Bytes per output file (K/M/G suffix)\n");
        shell_print("  -l LINES     Lines per output file (default: 1000)\n");
        shell_print("  -a N         Use N letters for suffix (default: 2)\n");
        shell_print("  -d           Use numeric suffixes instead of alphabetic\n");
        shell_print("\nIf FILE is '-' or omitted, read from stdin.\n");
        shell_print("Output files are named PREFIXaa, PREFIXab, ...\n");
        shell_last_exit_code = 1;
        return;
    }
    shell_print("split: stub (file splitting not yet implemented)\n");
    shell_print("  requested: ");
    shell_print(args);
    shell_print("\n");
    shell_last_exit_code = 0;
}

void cmd_join(const char *args) {
    if (!args || !*args) {
        shell_print("join - join lines of two files on a common field\n");
        shell_print("Usage: join [OPTION]... FILE1 FILE2\n");
        shell_print("  -1 FIELD     Join field in FILE1 (default: 1)\n");
        shell_print("  -2 FIELD     Join field in FILE2 (default: 1)\n");
        shell_print("  -t CHAR      Field separator (default: whitespace)\n");
        shell_print("  -i           Ignore case when comparing fields\n");
        shell_print("  -a N         Also print unpairable lines from FILE N\n");
        shell_print("  -e STRING    Replace missing fields with STRING\n");
        shell_last_exit_code = 1;
        return;
    }
    shell_print("join: stub (joining not yet implemented)\n");
    shell_print("  requested: ");
    shell_print(args);
    shell_print("\n");
    shell_last_exit_code = 0;
}

void cmd_hexdump(const char *file) {
    if (!file || !*file) {
        shell_print("hexdump - display file contents in hexadecimal\n");
        shell_print("Usage: hexdump [OPTION] FILE\n");
        shell_print("  -C           Canonical hex+ASCII display (16 bytes per row)\n");
        shell_print("  -n LENGTH    Show only first LENGTH bytes\n");
        shell_print("  -s OFFSET    Skip OFFSET bytes from the start\n");
        shell_print("  -v           Show all data (no collapsing of repeated lines)\n");
        shell_print("\nDefault format: hex address, 16 hex bytes, 16 ASCII chars.\n");
        shell_last_exit_code = 1;
        return;
    }
    shell_print("hexdump: stub (VFS read not yet wired)\n");
    shell_print("  requested file: ");
    shell_print(file);
    shell_print("\n");
    shell_print("  Use 'cat' or 'more' for raw text output.\n");
    shell_last_exit_code = 0;
}

void cmd_strings(const char *file) {
    if (!file || !*file) {
        shell_print("strings - display printable strings in a file\n");
        shell_print("Usage: strings [OPTION] FILE\n");
        shell_print("  -n LENGTH    Minimum string length (default: 4)\n");
        shell_print("  -a           Scan the whole file (default for object files)\n");
        shell_print("  -e ENCODING  Select encoding (s=7-bit, S=8-bit, b=16-bit, l=32-bit)\n");
        shell_print("  -o           Print offset before each string\n");
        shell_last_exit_code = 1;
        return;
    }
    shell_print("strings: stub (binary scanner not yet wired)\n");
    shell_print("  requested file: ");
    shell_print(file);
    shell_print("\n");
    shell_print("  Use 'hexdump' for hex/ASCII view.\n");
    shell_last_exit_code = 0;
}

void cmd_cksum(const char *args) {
    if (!args || !*args) {
        shell_print("cksum - checksum and count bytes in a file\n");
        shell_print("Usage: cksum [FILE]...\n");
        shell_print("  Computes a 32-bit CRC, total bytes, and prints one line per file.\n");
        shell_print("  Without arguments, reads from stdin.\n");
        shell_print("\n  Use 'md5sum' or 'sha256sum' for cryptographic hashes.\n");
        shell_last_exit_code = 1;
        return;
    }
    shell_print("cksum: stub (CRC routine not yet wired)\n");
    shell_print("  requested: ");
    shell_print(args);
    shell_print("\n");
    shell_last_exit_code = 0;
}
