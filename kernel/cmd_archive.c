/*
 * kernel/cmd_archive.c
 * cmd_tar, cmd_gzip, cmd_gunzip
 *
 * Archive / compression utilities.
 */

#include "cmd_archive.h"
#include "shell.h"
#include "shell_error.h"
#include "stdio.h"
#include "string.h"

void cmd_tar(const char *args) {
    if (!args || !*args) {
        shell_print("tar - archive utility\n");
        shell_print("Usage: tar <operation> [options] [file]...\n");
        shell_print("Operations:\n");
        shell_print("  -c            Create a new archive\n");
        shell_print("  -x            Extract files from an archive\n");
        shell_print("  -t            List archive contents\n");
        shell_print("  -r            Append files to the end of an archive\n");
        shell_print("Options:\n");
        shell_print("  -f FILE       Use archive file FILE\n");
        shell_print("  -v            Verbosely list files processed\n");
        shell_print("  -z            Filter the archive through gzip\n");
        shell_print("  -C DIR        Change to directory DIR before operations\n");
        shell_print("\nExamples:\n");
        shell_print("  tar -cf out.tar dir1 file2\n");
        shell_print("  tar -xf in.tar -C /tmp\n");
        shell_print("  tar -tzf archive.tar.gz\n");
        shell_last_exit_code = 1;
        return;
    }
    shell_print("tar: stub (full archive format not yet implemented)\n");
    shell_print("  requested: ");
    shell_print(args);
    shell_print("\n");
    shell_last_exit_code = 0;
}

void cmd_gzip(const char *args) {
    if (!args || !*args) {
        shell_print("gzip - compress files using LZ77 (DEFLATE)\n");
        shell_print("Usage: gzip [OPTION]... [FILE]...\n");
        shell_print("  -c            Write to stdout, keep original files\n");
        shell_print("  -d            Decompress (gunzip mode)\n");
        shell_print("  -1 .. -9      Compression level (1=fast, 9=best, default 6)\n");
        shell_print("  -k            Keep input files (do not delete originals)\n");
        shell_print("  -v            Verbose\n");
        shell_print("\nEach FILE is replaced by FILE.gz. With no FILE, read stdin.\n");
        shell_last_exit_code = 1;
        return;
    }
    shell_print("gzip: stub (DEFLATE codec not yet implemented)\n");
    shell_print("  requested: ");
    shell_print(args);
    shell_print("\n");
    shell_print("  Use 'gunzip' for decompression, 'tar -czf' for archives.\n");
    shell_last_exit_code = 0;
}

void cmd_gunzip(const char *args) {
    if (!args || !*args) {
        shell_print("gunzip - decompress gzip (.gz) files\n");
        shell_print("Usage: gunzip [OPTION]... [FILE]...\n");
        shell_print("  -c            Write to stdout, keep original files\n");
        shell_print("  -k            Keep input files\n");
        shell_print("  -v            Verbose\n");
        shell_print("  -t            Test compressed file integrity\n");
        shell_print("\nEach FILE.gz is replaced by FILE. With no FILE, read stdin.\n");
        shell_last_exit_code = 1;
        return;
    }
    shell_print("gunzip: stub (DEFLATE codec not yet implemented)\n");
    shell_print("  requested: ");
    shell_print(args);
    shell_print("\n");
    shell_print("  Equivalent to 'gzip -d'.\n");
    shell_last_exit_code = 0;
}
