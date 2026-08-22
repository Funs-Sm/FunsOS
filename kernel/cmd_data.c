/*
 * kernel/cmd_data.c - P5: Data utility commands
 * dd, split, join, hexdump, strings, cksum
 */

#include "cmd_data.h"
#include "shell.h"
#include "shell_error.h"
#include "string.h"
#include "stdio.h"
#include "stdlib.h"
#include "../fs/vfs.h"

/* ============================================================
 * cmd_dd - copy and convert file
 * ============================================================ */
void cmd_dd(const char *args) {
    const char *if_path = NULL;
    const char *of_path = NULL;
    uint32_t bs = 512;
    uint32_t count = 0;
    uint32_t skip = 0;
    uint32_t seek = 0;
    char buf[8192];
    uint32_t buf_size;
    file_t *in_file = NULL;
    file_t *out_file = NULL;
    int32_t ret;
    uint32_t total = 0;
    char line[128];

    if (!args || !*args) {
        shell_print("dd - convert and copy a file\n");
        shell_print("Usage: dd [OPERAND]...\n");
        shell_print("  if=FILE       Input file (default: stdin)\n");
        shell_print("  of=FILE       Output file (default: stdout)\n");
        shell_print("  bs=BYTES      Block size in bytes (default: 512)\n");
        shell_print("  count=N       Copy only N input blocks\n");
        shell_print("  skip=N        Skip N input blocks at start\n");
        shell_print("  seek=N        Skip N output blocks at start\n");
        shell_print("\nExamples:\n");
        shell_print("  dd if=/dev/zero of=disk.img bs=1M count=16\n");
        shell_print("  dd if=input.bin of=output.bin bs=4096\n");
        shell_last_exit_code = 1;
        return;
    }

    /* Parse operands */
    const char *p = args;
    while (*p) {
        while (*p == ' ') p++;
        if (strncmp(p, "if=", 3) == 0) {
            if_path = p + 3;
        } else if (strncmp(p, "of=", 3) == 0) {
            of_path = p + 3;
        } else if (strncmp(p, "bs=", 3) == 0) {
            bs = (uint32_t)atoi(p + 3);
        } else if (strncmp(p, "count=", 6) == 0) {
            count = (uint32_t)atoi(p + 6);
        } else if (strncmp(p, "skip=", 5) == 0) {
            skip = (uint32_t)atoi(p + 5);
        } else if (strncmp(p, "seek=", 5) == 0) {
            seek = (uint32_t)atoi(p + 5);
        }
        while (*p && *p != ' ') p++;
    }

    if (!if_path) {
        shell_print("dd: missing 'if=' (input file)\n");
        shell_last_exit_code = 1;
        return;
    }
    if (!of_path) {
        shell_print("dd: missing 'of=' (output file)\n");
        shell_last_exit_code = 1;
        return;
    }

    if (bs == 0) {
        shell_print("dd: bs cannot be zero\n");
        shell_last_exit_code = 1;
        return;
    }

    buf_size = bs < sizeof(buf) ? bs : sizeof(buf);

    /* Open input */
    ret = vfs_open(if_path, FILE_MODE_READ, &in_file);
    if (ret < 0) {
        shell_error(SHELL_ERR_FILE_NOT_FOUND, if_path);
        shell_last_exit_code = 1;
        return;
    }

    /* Open output */
    ret = vfs_open(of_path, FILE_MODE_WRITE | FILE_MODE_CREATE, &out_file);
    if (ret < 0) {
        vfs_close(in_file);
        shell_error(SHELL_ERR_CREATE_FAIL, of_path);
        shell_last_exit_code = 1;
        return;
    }

    /* Skip input blocks */
    if (skip > 0) {
        int64_t off = (int64_t)skip * (int64_t)bs;
        vfs_seek(in_file, (int32_t)off, SEEK_SET);
    }

    /* Seek output */
    if (seek > 0) {
        int64_t off = (int64_t)seek * (int64_t)bs;
        vfs_seek(out_file, (int32_t)off, SEEK_SET);
    }

    /* Copy loop */
    while (1) {
        uint32_t chunk = (bs < buf_size ? bs : buf_size);
        if (chunk == 0) break;

        int32_t r = vfs_read(in_file, buf, chunk);
        if (r <= 0) break;

        int32_t w = vfs_write(out_file, buf, (uint32_t)r);
        if (w < 0) break;

        total += (uint32_t)w;
        if (count > 0 && (total + bs - 1) / bs >= count) break;
        if ((uint32_t)r < chunk) break;
    }

    vfs_close(in_file);
    vfs_close(out_file);

    snprintf(line, sizeof(line), "%u bytes copied\n", total);
    shell_print(line);
    shell_last_exit_code = 0;
}

/* ============================================================
 * cmd_split - split file into pieces
 * ============================================================ */
static char next_suffix_char(char c) {
    if (c >= 'z') return 'a';
    if (c >= 'Z' && c < 'z') {
        if (c == 'Z') return 'a';
        return c + 1;
    }
    return (c >= 'a') ? c + 1 : 'a';
}

void cmd_split(const char *args) {
    const char *file_path = NULL;
    const char *prefix = "x";
    uint32_t lines_per_file = 1000;
    file_t *in_file = NULL;
    file_t *out_file = NULL;
    char line_buf[1024];
    char suffix[16];
    int32_t line_count = 0;
    int32_t ret;
    int done = 0;

    if (!args || !*args) {
        shell_print("split - split a file into pieces\n");
        shell_print("Usage: split [OPTION] [FILE [PREFIX]]\n");
        shell_print("  -l LINES     Lines per output file (default: 1000)\n");
        shell_print("  -b SIZE      Bytes per output file (K/M/G suffix) - not yet\n");
        shell_print("\nOutput files are named PREFIXaa, PREFIXab, ...\n");
        shell_last_exit_code = 1;
        return;
    }

    /* Simple parse: split [-l N] FILE [PREFIX] */
    const char *p = args;
    while (*p == ' ') p++;

    if (strncmp(p, "-l", 2) == 0 && (p[2] == ' ' || p[2] == '\0')) {
        if (p[2] == ' ') {
            lines_per_file = (uint32_t)atoi(p + 3);
        }
        /* advance past -l N */
        if (p[2] == ' ') {
            const char *q = p + 3;
            while (*q && *q != ' ') q++;
            p = q;
        } else if (p[2] == '\0') {
            p++;
            while (*p && *p == ' ') p++;
            if (*p) {
                lines_per_file = (uint32_t)atoi(p);
                while (*p && *p != ' ') p++;
            }
        }
        while (*p == ' ') p++;
    }

    /* Next token: file */
    if (*p) {
        file_path = p;
        while (*p && *p != ' ') p++;
    }
    if (!file_path || !*file_path) {
        shell_print("split: missing file operand\n");
        shell_last_exit_code = 1;
        return;
    }

    /* Next token: prefix */
    while (*p == ' ') p++;
    if (*p) {
        prefix = p;
    }

    /* Open input */
    ret = vfs_open(file_path, FILE_MODE_READ, &in_file);
    if (ret < 0) {
        shell_error(SHELL_ERR_FILE_NOT_FOUND, file_path);
        shell_last_exit_code = 1;
        return;
    }

    suffix[0] = 'a';
    suffix[1] = 'a';
    suffix[2] = '\0';

    while (!done) {
        char out_path[300];
        int slen = strlen(suffix);

        snprintf(out_path, sizeof(out_path), "%s%s", prefix, suffix);

        ret = vfs_open(out_path, FILE_MODE_WRITE | FILE_MODE_CREATE, &out_file);
        if (ret < 0) {
            shell_error(SHELL_ERR_CREATE_FAIL, out_path);
            vfs_close(in_file);
            shell_last_exit_code = 1;
            return;
        }

        line_count = 0;
        while (line_count < (int32_t)lines_per_file) {
            int i = 0;
            int c;
            while (i < (int)sizeof(line_buf) - 1) {
                int32_t r = vfs_read(in_file, &c, 1);
                if (r <= 0) {
                    done = 1;
                    break;
                }
                line_buf[i++] = (char)c;
                if ((char)c == '\n') break;
            }
            if (i == 0) break;
            line_buf[i] = '\0';
            vfs_write(out_file, line_buf, (uint32_t)i);
            line_count++;
        }

        vfs_close(out_file);

        if (done) break;

        /* Advance suffix */
        int carry = 1;
        for (int i = slen - 1; i >= 0 && carry; i--) {
            char nc = next_suffix_char(suffix[i]);
            if (nc != suffix[i] + 1 && !(suffix[i] == 'z' && nc == 'a')) {
                /* wrapped */
            }
            suffix[i] = nc;
            if (nc == 'a' && i == slen - 1) {
                carry = 1;
            } else {
                carry = 0;
            }
        }
        if (carry && slen < 4) {
            suffix[slen] = 'a';
            suffix[slen + 1] = '\0';
        }
    }

    vfs_close(in_file);
    shell_last_exit_code = 0;
}

/* ============================================================
 * cmd_join - join two files on common field
 * Simplified: TAB-separated first field join
 * ============================================================ */
static int get_first_field(const char *line, char *field, int field_size) {
    int i = 0;
    while (line[i] && line[i] != '\t' && line[i] != '\n' && line[i] != '\r' && i < field_size - 1) {
        field[i] = line[i];
        i++;
    }
    field[i] = '\0';
    return i;
}

void cmd_join(const char *args) {
    const char *file1 = NULL;
    const char *file2 = NULL;
    file_t *fp1 = NULL;
    file_t *fp2 = NULL;
    char line1[1024];
    char line2[1024];
    char f1[256], f2[256];
    int eof1 = 0, eof2 = 0;
    int32_t r1, r2;
    char out_buf[2048];

    if (!args || !*args) {
        shell_print("join - join lines of two files on a common field\n");
        shell_print("Usage: join FILE1 FILE2\n");
        shell_print("  Joins lines from FILE1 and FILE2 with matching TAB-separated first fields.\n");
        shell_last_exit_code = 1;
        return;
    }

    /* Parse two file arguments */
    const char *p = args;
    while (*p == ' ') p++;
    if (*p) {
        file1 = p;
        while (*p && *p != ' ') p++;
    }
    while (*p == ' ') p++;
    if (*p) {
        file2 = p;
    }

    if (!file1 || !file2 || !*file1 || !*file2) {
        shell_print("join: missing file operands\n");
        shell_last_exit_code = 1;
        return;
    }

    if (strcmp(file1, "-") != 0) {
        int32_t ret = vfs_open(file1, FILE_MODE_READ, &fp1);
        if (ret < 0) {
            shell_error(SHELL_ERR_FILE_NOT_FOUND, file1);
            shell_last_exit_code = 1;
            return;
        }
    }
    if (strcmp(file2, "-") != 0) {
        int32_t ret = vfs_open(file2, FILE_MODE_READ, &fp2);
        if (ret < 0) {
            if (fp1) vfs_close(fp1);
            shell_error(SHELL_ERR_FILE_NOT_FOUND, file2);
            shell_last_exit_code = 1;
            return;
        }
    }

    /* Read first lines */
    if (fp1) {
        int i = 0;
        while (i < (int)sizeof(line1) - 1) {
            r1 = vfs_read(fp1, &line1[i], 1);
            if (r1 <= 0) { eof1 = 1; line1[i] = '\0'; break; }
            if (line1[i] == '\n') { line1[i + 1] = '\0'; break; }
            i++;
        }
    }
    if (fp2) {
        int i = 0;
        while (i < (int)sizeof(line2) - 1) {
            r2 = vfs_read(fp2, &line2[i], 1);
            if (r2 <= 0) { eof2 = 1; line2[i] = '\0'; break; }
            if (line2[i] == '\n') { line2[i + 1] = '\0'; break; }
            i++;
        }
    }

    while (!eof1 && !eof2) {
        get_first_field(line1, f1, sizeof(f1));
        get_first_field(line2, f2, sizeof(f2));

        int cmp = strcmp(f1, f2);
        if (cmp == 0) {
            /* Strip trailing newlines for clean output */
            int l1 = strlen(line1);
            if (l1 > 0 && line1[l1 - 1] == '\n') line1[l1 - 1] = '\0';
            int l2 = strlen(line2);
            if (l2 > 0 && line2[l2 - 1] == '\n') line2[l2 - 1] = '\0';
            snprintf(out_buf, sizeof(out_buf), "%s %s\n", line1, line2);
            shell_print(out_buf);
        }

        if (cmp <= 0) {
            /* Advance file1 */
            int i = 0;
            while (i < (int)sizeof(line1) - 1) {
                r1 = vfs_read(fp1, &line1[i], 1);
                if (r1 <= 0) { eof1 = 1; line1[i] = '\0'; break; }
                if (line1[i] == '\n') { line1[i + 1] = '\0'; break; }
                i++;
            }
        }
        if (cmp >= 0) {
            /* Advance file2 */
            int i = 0;
            while (i < (int)sizeof(line2) - 1) {
                r2 = vfs_read(fp2, &line2[i], 1);
                if (r2 <= 0) { eof2 = 1; line2[i] = '\0'; break; }
                if (line2[i] == '\n') { line2[i + 1] = '\0'; break; }
                i++;
            }
        }
    }

    if (fp1) vfs_close(fp1);
    if (fp2) vfs_close(fp2);
    shell_last_exit_code = 0;
}

/* ============================================================
 * cmd_hexdump - hex + ASCII dump
 * -C: canonical (hex+ascii on same line)
 * -n LENGTH: show only first N bytes
 * ============================================================ */
void cmd_hexdump(const char *args) {
    const char *file_path = NULL;
    int canonical = 0;
    uint32_t limit = 0;
    file_t *file = NULL;
    uint8_t buf[16];
    uint32_t addr = 0;
    int32_t n;
    char line[128];

    if (!args || !*args) {
        shell_print("hexdump - display file contents in hexadecimal\n");
        shell_print("Usage: hexdump [OPTION] FILE\n");
        shell_print("  -C           Canonical hex+ASCII display (16 bytes per row)\n");
        shell_print("  -n LENGTH    Show only first LENGTH bytes\n");
        shell_print("  -s OFFSET    Skip OFFSET bytes from the start (not yet)\n");
        shell_print("  -v           Show all data (default)\n");
        shell_print("\nDefault format: hex address, 16 hex bytes, 16 ASCII chars.\n");
        shell_last_exit_code = 1;
        return;
    }

    /* Parse options */
    const char *p = args;
    while (*p == ' ') p++;
    while (*p == '-') {
        p++;
        while (*p && *p != ' ' && *p != '-') p++;
        while (*p == ' ') p++;
        if (!*p) {
            shell_print("hexdump: missing argument after option\n");
            shell_last_exit_code = 1;
            return;
        }
        if (p[-1] == 'C') {
            canonical = 1;
        } else if (p[-1] == 'n') {
            while (*p == ' ') p++;
            limit = (uint32_t)atoi(p);
            while (*p && *p != ' ') p++;
        }
        while (*p == ' ') p++;
        if (!*p || *p != '-') break;
    }
    if (*p) {
        file_path = p;
    }

    if (!file_path || !*file_path) {
        shell_print("hexdump: missing file operand\n");
        shell_last_exit_code = 1;
        return;
    }

    int32_t ret = vfs_open(file_path, FILE_MODE_READ, &file);
    if (ret < 0) {
        shell_error(SHELL_ERR_FILE_NOT_FOUND, file_path);
        shell_last_exit_code = 1;
        return;
    }

    if (canonical) {
        /* Canonical: each line: OFFSET  HEX16  |ASCII16| */
        while (1) {
            if (limit > 0 && addr >= limit) break;
            uint32_t chunk = (limit > 0 && limit - addr < 16) ? (limit - addr) : 16;
            n = vfs_read(file, buf, chunk);
            if (n <= 0) break;

            snprintf(line, sizeof(line), "%08x  ", addr);
            shell_print(line);

            for (int i = 0; i < 16; i++) {
                if (i < n) {
                    snprintf(line, sizeof(line), "%02x ", buf[i]);
                    shell_print(line);
                } else {
                    shell_print("   ");
                }
                if (i == 7) shell_print(" ");
            }
            shell_print(" |");
            for (int i = 0; i < n; i++) {
                char c = (buf[i] >= 32 && buf[i] < 127) ? (char)buf[i] : '.';
                snprintf(line, sizeof(line), "%c", c);
                shell_print(line);
            }
            for (int i = n; i < 16; i++) shell_print(" ");
            shell_print("|\n");
            addr += (uint32_t)n;
            if (n < (int)chunk) break;
        }
    } else {
        /* Default: offset column + hex + ascii */
        while (1) {
            if (limit > 0 && addr >= limit) break;
            uint32_t chunk = (limit > 0 && limit - addr < 16) ? (limit - addr) : 16;
            n = vfs_read(file, buf, chunk);
            if (n <= 0) break;

            snprintf(line, sizeof(line), "%08x ", addr);
            shell_print(line);

            for (int i = 0; i < n; i++) {
                snprintf(line, sizeof(line), "%02x", buf[i]);
                shell_print(line);
                if (i == 7) shell_print(" ");
                else shell_print(" ");
            }
            for (int i = n; i < 16; i++) {
                shell_print("   ");
                if (i == 7) shell_print(" ");
            }
            shell_print("  ");
            for (int i = 0; i < n; i++) {
                char c = (buf[i] >= 32 && buf[i] < 127) ? (char)buf[i] : '.';
                snprintf(line, sizeof(line), "%c", c);
                shell_print(line);
            }
            shell_print("\n");
            addr += (uint32_t)n;
            if (n < (int)chunk) break;
        }
    }

    vfs_close(file);
    shell_last_exit_code = 0;
}

/* ============================================================
 * cmd_strings - scan printable strings in a file
 * -n LENGTH: minimum string length (default 4)
 * ============================================================ */
void cmd_strings(const char *args) {
    const char *file_path = NULL;
    uint32_t min_len = 4;
    int show_offset = 0;
    file_t *file = NULL;
    uint8_t ch;
    char buf[256];
    int in_string = 0;
    int str_len = 0;
    uint32_t str_start = 0;
    uint32_t offset = 0;
    int32_t r;
    char line[128];

    if (!args || !*args) {
        shell_print("strings - display printable strings in a file\n");
        shell_print("Usage: strings [OPTION] FILE\n");
        shell_print("  -n LENGTH    Minimum string length (default: 4)\n");
        shell_print("  -o           Print offset before each string\n");
        shell_print("  -a           Scan all (default)\n");
        shell_last_exit_code = 1;
        return;
    }

    /* Parse options */
    const char *p = args;
    while (*p == ' ') p++;
    while (*p == '-') {
        p++;
        char opt = *p;
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
        if (opt == 'n') {
            min_len = (uint32_t)atoi(p);
            while (*p && *p != ' ') p++;
        } else if (opt == 'o') {
            show_offset = 1;
        }
        while (*p == ' ') p++;
        if (!*p || *p != '-') break;
    }
    if (*p) {
        file_path = p;
    }

    if (!file_path || !*file_path) {
        shell_print("strings: missing file operand\n");
        shell_last_exit_code = 1;
        return;
    }

    if (min_len < 1) min_len = 1;

    int32_t ret = vfs_open(file_path, FILE_MODE_READ, &file);
    if (ret < 0) {
        shell_error(SHELL_ERR_FILE_NOT_FOUND, file_path);
        shell_last_exit_code = 1;
        return;
    }

    offset = 0;
    while (1) {
        r = vfs_read(file, &ch, 1);
        if (r <= 0) {
            if (in_string && str_len >= (int)min_len) {
                buf[str_len] = '\0';
                if (show_offset) {
                    snprintf(line, sizeof(line), "%u %s\n", str_start, buf);
                } else {
                    snprintf(line, sizeof(line), "%s\n", buf);
                }
                shell_print(line);
            }
            break;
        }

        int is_print = (ch >= 32 && ch < 127);
        if (is_print) {
            if (!in_string) {
                in_string = 1;
                str_start = offset;
                str_len = 0;
            }
            if (str_len < (int)sizeof(buf) - 1) {
                buf[str_len++] = (char)ch;
            }
        } else {
            if (in_string) {
                in_string = 0;
                if (str_len >= (int)min_len) {
                    buf[str_len] = '\0';
                    if (show_offset) {
                        snprintf(line, sizeof(line), "%u %s\n", str_start, buf);
                    } else {
                        snprintf(line, sizeof(line), "%s\n", buf);
                    }
                    shell_print(line);
                }
                str_len = 0;
            }
        }
        offset++;
    }

    vfs_close(file);
    shell_last_exit_code = 0;
}

/* ============================================================
 * cmd_cksum - POSIX CRC32 checksum
 * Polynomial: 0x04C11DB7 (reflected: 0xEDB88320)
 * Output: CRC SIZE FILE
 * ============================================================ */
static uint32_t crc32_table[256];
static int crc32_table_init = 0;

static void init_crc32_table(void) {
    if (crc32_table_init) return;
    crc32_table_init = 1;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++) {
            if (c & 1) {
                c = 0xEDB88320U ^ (c >> 1);
            } else {
                c = c >> 1;
            }
        }
        crc32_table[i] = c;
    }
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        uint8_t byte = data[i];
        uint32_t tbl_idx = (crc ^ byte) & 0xFF;
        crc = crc32_table[tbl_idx] ^ (crc >> 8);
    }
    return crc;
}

static uint32_t crc32_final(uint32_t crc) {
    return crc ^ 0xFFFFFFFFU;
}

static void do_cksum_file(const char *path) {
    init_crc32_table();
    file_t *file = NULL;
    uint32_t crc = 0xFFFFFFFF;
    uint8_t buf[1024];
    uint32_t total = 0;
    int32_t r;

    int32_t ret = vfs_open(path, FILE_MODE_READ, &file);
    if (ret < 0) {
        shell_error(SHELL_ERR_FILE_NOT_FOUND, path);
        shell_last_exit_code = 1;
        return;
    }

    while (1) {
        r = vfs_read(file, buf, sizeof(buf));
        if (r <= 0) break;
        crc = crc32_update(crc, buf, (uint32_t)r);
        total += (uint32_t)r;
    }

    vfs_close(file);
    crc = crc32_final(crc);

    char line[256];
    snprintf(line, sizeof(line), "%u %u %s\n", crc, total, path);
    shell_print(line);
}

void cmd_cksum(const char *args) {
    if (!args || !*args) {
        shell_print("cksum - checksum and count bytes in a file\n");
        shell_print("Usage: cksum [FILE]...\n");
        shell_print("  Computes a 32-bit CRC (POSIX style), total bytes.\n");
        shell_print("  Output: CRC SIZE FILE\n");
        shell_last_exit_code = 1;
        return;
    }

    /* Parse space-separated file list */
    const char *p = args;
    const char *file_list[64];
    int file_count = 0;

    while (*p == ' ') p++;
    while (*p && file_count < 64) {
        file_list[file_count++] = p;
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
    }

    if (file_count == 0) {
        shell_print("cksum: missing file operand\n");
        shell_last_exit_code = 1;
        return;
    }

    shell_last_exit_code = 0;
    for (int i = 0; i < file_count; i++) {
        do_cksum_file(file_list[i]);
    }
}
