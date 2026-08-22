/*
 * kernel/cmd_text.c - P4: Text utility commands
 * cut, paste, tr, rev, fold, expand, unexpand, nl, look, comm, tsort
 */

#include "cmd_text.h"
#include "shell.h"
#include "shell_error.h"
#include "string.h"
#include "stdio.h"
#include "stdlib.h"
#include "vfs.h"

#define MAX_LINE 1024

/* compat shim: cmd_text was written against a single-arg shell_error
 * but the real API is shell_error(int code, const char *ctx).  Wrap it. */
static void text_err(const char *msg)
{
    shell_print(msg);
    shell_last_exit_code = 1;
}
#define shell_error(msg) text_err(msg)

/* =========================================================================
 * Helper: load all lines from a file into a buffer of char* pointers.
 * Returns number of lines, or -1 on error.
 * Caller is responsible for freeing both the pointers array and each line.
 * ========================================================================= */
static int load_lines(const char *path, char ***lines_out)
{
    file_t *file = NULL;
    int32_t err = vfs_open(path, FILE_MODE_READ, &file);
    if (err < 0) {
        shell_error("cannot open file: ");
        shell_error(path);
        shell_error("\n");
        *lines_out = NULL;
        return -1;
    }

    char **lines = NULL;
    int capacity = 64;
    int count = 0;

    lines = (char **)malloc(sizeof(char *) * capacity);
    if (!lines) {
        vfs_close(file);
        *lines_out = NULL;
        return -1;
    }

    char buf[MAX_LINE];
    int32_t n;

    while ((n = vfs_read(file, buf, MAX_LINE - 1)) > 0) {
        buf[n] = '\0';
        char *start = buf;
        char *p = buf;

        while (*p) {
            if (*p == '\n' || *p == '\r') {
                if (p > start) {
                    if (count >= capacity) {
                        capacity *= 2;
                        char **tmp = (char **)realloc(lines, sizeof(char *) * capacity);
                        if (!tmp) {
                            for (int i = 0; i < count; i++) free(lines[i]);
                            free(lines);
                            vfs_close(file);
                            *lines_out = NULL;
                            return -1;
                        }
                        lines = tmp;
                    }
                    size_t len = p - start;
                    lines[count] = (char *)malloc(len + 1);
                    if (!lines[count]) {
                        for (int i = 0; i < count; i++) free(lines[i]);
                        free(lines);
                        vfs_close(file);
                        *lines_out = NULL;
                        return -1;
                    }
                    memcpy(lines[count], start, len);
                    lines[count][len] = '\0';
                    count++;
                }
                if (*p == '\r' && p[1] == '\n') p++;
                start = p + 1;
            }
            p++;
        }

        if (start <= buf + n) {
        }
    }

    if (n < 0) {
        for (int i = 0; i < count; i++) free(lines[i]);
        free(lines);
        vfs_close(file);
        *lines_out = NULL;
        return -1;
    }

    if (count > 0 && count < capacity) {
        char **tmp = (char **)realloc(lines, sizeof(char *) * count);
        if (tmp) lines = tmp;
    }

    vfs_close(file);
    *lines_out = lines;
    return count;
}

/* =========================================================================
 * Helper: write all lines back (used by commands that need to re-emit data)
 * ========================================================================= */
static void free_lines(char **lines, int count)
{
    if (!lines) return;
    for (int i = 0; i < count; i++) {
        if (lines[i]) free(lines[i]);
    }
    free(lines);
}

/* =========================================================================
 * cmd_cut  -  -d CHAR -f LIST FILE
 * LIST: comma-separated, e.g. 1,3-5,7-
 * Default delimiter: TAB
 * ========================================================================= */
void cmd_cut(const char *args)
{
    if (!args || !*args) {
        shell_print("cut - print selected parts of lines\n");
        shell_print("Usage: cut -d DELIM -f LIST [FILE]\n");
        shell_print("  -d CHAR    Field delimiter (default: TAB)\n");
        shell_print("  -f LIST    Field numbers to extract (e.g. 1,3-5,7-)\n");
        shell_last_exit_code = 1;
        return;
    }

    char delim = '\t';
    char *field_str = NULL;
    char *filename = NULL;

    const char *p = args;
    while (*p) {
        while (*p == ' ') p++;
        if (*p != '-') {
            filename = (char *)p;
            break;
        }
        if (p[1] == 'd' && p[2] == ' ') {
            p += 3;
            delim = *p;
            while (*p && *p != ' ') p++;
        } else if (p[1] == 'f' && p[2] == ' ') {
            p += 3;
            field_str = (char *)p;
            char *end = (char *)p;
            while (*end && *end != ' ') end++;
            if (*end) { *end = '\0'; filename = end + 1; }
            else filename = NULL;
            while (*p) p++;
        } else {
            shell_error("cut: unknown option\n");
            shell_last_exit_code = 1;
            return;
        }
        while (*p == ' ') p++;
        if (!*p) break;
    }

    if (!field_str) {
        shell_error("rev: missing file operand\n");
        shell_last_exit_code = 1;
        return;
    }

    while (*filename == ' ') filename++;
    char *end = filename;
    while (*end && *end != ' ') end++;
    char saved = *end;
    char fname_buf[MAX_LINE];
    if (filename && *filename) {
        size_t len = end - filename;
        if (len >= MAX_LINE) len = MAX_LINE - 1;
        memcpy(fname_buf, filename, len);
        fname_buf[len] = '\0';
        if (saved) filename = fname_buf;
    } else {
        filename = NULL;
    }

    if (!filename || !*filename) {
        shell_error("cut: missing file operand\n");
        shell_last_exit_code = 1;
        return;
    }

    char **lines = NULL;
    int line_count = load_lines(filename, &lines);
    if (line_count < 0) {
        shell_last_exit_code = 1;
        return;
    }

    /* Parse field list into array */
    int fields[256];
    int field_count = 0;
    char *list = field_str;
    while (*list) {
        char *dash = NULL;
        char num_buf[16];
        char *np = num_buf;

        while (*list == ' ') list++;
        if (*list == ',') { list++; continue; }
        if (!*list || *list == ' ') break;

        if (*list == '-') {
            fields[field_count++] = -1;
            list++;
            while (*list && *list >= '0' && *list <= '9') list++;
            continue;
        }

        while (*list >= '0' && *list <= '9') {
            *np++ = *list++;
        }
        *np = '\0';
        int start = atoi(num_buf);
        np = num_buf;

        if (*list == '-') {
            dash = (char *)list;
            list++;
            while (*list >= '0' && *list <= '9') {
                *np++ = *list++;
            }
            *np = '\0';
            int end = atoi(num_buf);
            for (int i = start; i <= end; i++) {
                if (field_count < 255) fields[field_count++] = i;
            }
        } else {
            fields[field_count++] = start;
        }
    }

    char out_buf[MAX_LINE];
    for (int i = 0; i < line_count; i++) {
        char *line = lines[i];
        int field_num = 1;
        char *token_start = line;
        char *token_end = line;
        int first = 1;

        while (*token_end) {
            if (*token_end == delim) {
                int in_range = 0;
                for (int f = 0; f < field_count; f++) {
                    if (fields[f] == field_num || fields[f] == -1) { in_range = 1; break; }
                }
                if (in_range) {
                    if (!first) {
                        char tmp[2] = { delim, '\0' };
                        shell_print(tmp);
                    }
                    size_t tok_len = token_end - token_start;
                    if (tok_len >= MAX_LINE) tok_len = MAX_LINE - 1;
                    memcpy(out_buf, token_start, tok_len);
                    out_buf[tok_len] = '\0';
                    shell_print(out_buf);
                    first = 0;
                }
                field_num++;
                token_start = token_end + 1;
                token_end = token_start;
            } else {
                token_end++;
            }
        }

        int in_range = 0;
        for (int f = 0; f < field_count; f++) {
            if (fields[f] == field_num || fields[f] == -1) { in_range = 1; break; }
        }
        if (in_range) {
            if (!first) {
                char tmp[2] = { delim, '\0' };
                shell_print(tmp);
            }
            size_t tok_len = token_end - token_start;
            if (tok_len >= MAX_LINE) tok_len = MAX_LINE - 1;
            memcpy(out_buf, token_start, tok_len);
            out_buf[tok_len] = '\0';
            shell_print(out_buf);
        }
        shell_print("\n");
    }

    free_lines(lines, line_count);
    shell_last_exit_code = 0;
}

/* =========================================================================
 * cmd_paste  -  parallel paste of multiple files (TAB separated)
 * ========================================================================= */
void cmd_paste(const char *args)
{
    if (!args || !*args) {
        shell_print("paste - merge lines of files in parallel\n");
        shell_print("Usage: paste [OPTION]... [FILE]...\n");
        shell_print("  -d CHAR    Delimiter between columns (default: TAB)\n");
        shell_last_exit_code = 1;
        return;
    }

    /* Parse file arguments */
    char *files[64];
    int file_count = 0;

    const char *p = args;
    while (*p && file_count < 64) {
        while (*p == ' ') p++;
        if (!*p) break;
        if (*p == '-') {
            if (p[1] == 'd' && p[2] == ' ') {
                p += 3;
                p++;
                while (*p && *p != ' ') p++;
            } else {
                while (*p && *p != ' ') p++;
            }
            while (*p == ' ') p++;
            continue;
        }
        char *start = (char *)p;
        while (*p && *p != ' ') p++;
        size_t len = p - start;
        files[file_count] = (char *)malloc(len + 1);
        if (!files[file_count]) { file_count = 0; break; }
        memcpy(files[file_count], start, len);
        files[file_count][len] = '\0';
        file_count++;
        while (*p == ' ') p++;
    }

    if (file_count == 0) {
        shell_error("paste: missing file operands\n");
        shell_last_exit_code = 1;
        return;
    }

    /* Open all files */
    file_t *fps[64];
    for (int i = 0; i < file_count; i++) fps[i] = NULL;

    for (int i = 0; i < file_count; i++) {
        int32_t err = vfs_open(files[i], FILE_MODE_READ, &fps[i]);
        if (err < 0) {
            shell_error("paste: cannot open file: ");
            shell_error(files[i]);
            shell_error("\n");
            for (int j = 0; j < i; j++) vfs_close(fps[j]);
            for (int j = 0; j < file_count; j++) free(files[j]);
            shell_last_exit_code = 1;
            return;
        }
    }

    char bufs[64][MAX_LINE];
    int active = file_count;
    char delim = '\t';

    while (active > 0) {
        for (int i = 0; i < file_count; i++) {
            if (!fps[i]) {
                bufs[i][0] = '\0';
                continue;
            }
            int32_t n = vfs_read(fps[i], bufs[i], MAX_LINE - 1);
            if (n <= 0) {
                bufs[i][0] = '\0';
                vfs_close(fps[i]);
                fps[i] = NULL;
                active--;
                continue;
            }
            bufs[i][n] = '\0';
            char *nl = bufs[i];
            while (*nl && *nl != '\n' && *nl != '\r') nl++;
            *nl = '\0';
            if (nl > bufs[i] && (nl[-1] == '\r' || nl[-1] == '\n')) {
                nl[-1] = '\0';
            }
        }

        for (int i = 0; i < file_count; i++) {
            shell_print(bufs[i]);
            if (i < file_count - 1) {
                char d[2] = { delim, '\0' };
                shell_print(d);
            }
        }
        shell_print("\n");
    }

    for (int i = 0; i < file_count; i++) {
        if (fps[i]) vfs_close(fps[i]);
        free(files[i]);
    }
    shell_last_exit_code = 0;
}

/* =========================================================================
 * cmd_tr  -  translate SET1 to SET2 (equal-length character replacement)
 * ========================================================================= */
void cmd_tr(const char *args)
{
    if (!args || !*args) {
        shell_print("tr - translate or delete characters\n");
        shell_print("Usage: tr SET1 SET2\n");
        shell_print("  Translates characters in SET1 to corresponding characters in SET2.\n");
        shell_last_exit_code = 1;
        return;
    }

    /* Read SET1 and SET2 */
    char set1[MAX_LINE], set2[MAX_LINE];
    char out_buf[MAX_LINE];

    const char *p = args;
    int s1_len = 0, s2_len = 0;
    int in_set1 = 1;

    while (*p == ' ') p++;

    while (*p) {
        if (*p == '\\' && p[1]) {
            p++;
            if (*p == 'n') { if (in_set1) set1[s1_len++] = '\n'; else set2[s2_len++] = '\n'; }
            else if (*p == 't') { if (in_set1) set1[s1_len++] = '\t'; else set2[s2_len++] = '\t'; }
            else if (*p == '\\') { if (in_set1) set1[s1_len++] = '\\'; else set2[s2_len++] = '\\'; }
            else if (*p >= '0' && *p <= '7') {
                int val = 0;
                for (int i = 0; i < 3 && *p >= '0' && *p <= '7'; i++, p++) {
                    val = val * 8 + (*p - '0');
                }
                if (in_set1) set1[s1_len++] = (char)val; else set2[s2_len++] = (char)val;
                continue;
            } else {
                if (in_set1) set1[s1_len++] = *p; else set2[s2_len++] = *p;
            }
            p++;
            continue;
        }

        if (*p == ' ') {
            if (s1_len > 0 && s2_len == 0) {
                in_set1 = 0;
            }
            p++;
            continue;
        }

        if (in_set1) {
            if (s1_len < MAX_LINE - 1) set1[s1_len++] = *p;
        } else {
            if (s2_len < MAX_LINE - 1) set2[s2_len++] = *p;
        }
        p++;
    }

    set1[s1_len] = '\0';
    set2[s2_len] = '\0';

    if (s1_len == 0) {
        shell_error("tr: empty string\n");
        shell_last_exit_code = 1;
        return;
    }

    /* Build translation table */
    unsigned char trans[256];
    for (int i = 0; i < 256; i++) trans[i] = (unsigned char)i;

    for (int i = 0; i < s1_len && i < s2_len; i++) {
        trans[(unsigned char)set1[i]] = (unsigned char)set2[i];
    }

    /* Read from stdin (simulate single-file read) */
    file_t *file = NULL;
    int32_t err = vfs_open("/dev/stdin", FILE_MODE_READ, &file);
    if (err < 0) {
        shell_error("tr: cannot open stdin\n");
        shell_last_exit_code = 1;
        return;
    }

    char buf[MAX_LINE];
    int out_pos = 0;
    int32_t n;

    while ((n = vfs_read(file, buf, MAX_LINE - 1)) > 0) {
        for (int i = 0; i < n; i++) {
            char c = (char)trans[(unsigned char)buf[i]];
            if (c == '\n' || c == '\r' || out_pos >= MAX_LINE - 1) {
                out_buf[out_pos] = '\0';
                shell_print(out_buf);
                shell_print("\n");
                out_pos = 0;
            } else if (c != '\n' && c != '\r') {
                out_buf[out_pos++] = c;
            }
        }
    }

    if (out_pos > 0) {
        out_buf[out_pos] = '\0';
        shell_print(out_buf);
        shell_print("\n");
    }

    vfs_close(file);
    shell_last_exit_code = 0;
}

/* =========================================================================
 * cmd_rev  -  reverse each line
 * ========================================================================= */
void cmd_rev(const char *file)
{
    if (!file || !*file) {
        shell_print("rev - reverse lines of a file\n");
        shell_print("Usage: rev [FILE]\n");
        shell_error("rev: missing file operand\n");
        shell_last_exit_code = 1;
        return;
    }

    while (*file == ' ') file++;
    char *end = (char *)file;
    while (*end && *end != ' ') end++;
    char fname[MAX_LINE];
    size_t len = end - file;
    if (len >= MAX_LINE) len = MAX_LINE - 1;
    memcpy(fname, file, len);
    fname[len] = '\0';
    if (!*fname) {
        shell_error("rev: missing file operand\n");
        shell_last_exit_code = 1;
        return;
    }

    char **lines = NULL;
    int count = load_lines(fname, &lines);
    if (count < 0) {
        shell_last_exit_code = 1;
        return;
    }

    for (int i = 0; i < count; i++) {
        int len2 = 0;
        while (lines[i][len2]) len2++;

        for (int j = 0; j < len2 / 2; j++) {
            char tmp = lines[i][j];
            lines[i][j] = lines[i][len2 - 1 - j];
            lines[i][len2 - 1 - j] = tmp;
        }
        shell_print(lines[i]);
        shell_print("\n");
    }

    free_lines(lines, count);
    shell_last_exit_code = 0;
}

/* =========================================================================
 * cmd_fold  -  -w WIDTH (default 80), break at spaces (-s)
 * ========================================================================= */
void cmd_fold(const char *args)
{
    if (!args || !*args) {
        shell_print("fold - wrap each line to a maximum width\n");
        shell_print("Usage: fold [-w WIDTH] [-s] [FILE]\n");
        shell_print("  -w WIDTH   Line width (default: 80)\n");
        shell_print("  -s         Break at spaces\n");
        shell_last_exit_code = 1;
        return;
    }

    int width = 80;
    int break_at_spaces = 0;
    char *filename = NULL;

    const char *p = args;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;

        if (*p == '-') {
            if (p[1] == 'w' && p[2] == ' ') {
                p += 3;
                char wbuf[16];
                int wi = 0;
                while (*p >= '0' && *p <= '9' && wi < 15) wbuf[wi++] = *p++;
                wbuf[wi] = '\0';
                width = atoi(wbuf);
                if (width <= 0) width = 80;
            } else if (p[1] == 's' && (p[2] == ' ' || !p[2])) {
                break_at_spaces = 1;
                p += 2;
            } else {
                while (*p && *p != ' ') p++;
            }
        } else {
            filename = (char *)p;
            while (*p && *p != ' ') p++;
            break;
        }
    }

    while (*filename == ' ') filename++;
    char *end = filename;
    while (*end && *end != ' ') end++;
    char fname[MAX_LINE];
    size_t len = end - filename;
    if (len >= MAX_LINE) len = MAX_LINE - 1;
    memcpy(fname, filename, len);
    fname[len] = '\0';

    if (!fname[0]) {
        shell_error("fold: missing file operand\n");
        shell_last_exit_code = 1;
        return;
    }

    char **lines = NULL;
    int count = load_lines(fname, &lines);
    if (count < 0) {
        shell_last_exit_code = 1;
        return;
    }

    for (int i = 0; i < count; i++) {
        int len2 = 0;
        while (lines[i][len2]) len2++;

        int pos = 0;
        int line_start = 0;

        while (pos < len2) {
            int chunk = width;
            if (pos + chunk > len2) chunk = len2 - pos;

            if (break_at_spaces && pos > 0) {
                int break_point = pos + width;
                if (break_point > len2) break_point = len2;
                int found = -1;
                for (int j = pos + width; j > pos; j--) {
                    if (lines[i][j] == ' ') { found = j; break; }
                }
                if (found >= 0 && found < break_point) {
                    chunk = found - pos;
                }
            }

            char saved = lines[i][pos + chunk];
            if (pos + chunk < len2 && break_at_spaces) {
                int bp = pos + chunk - 1;
                while (bp >= pos && lines[i][bp] != ' ') bp--;
                if (bp >= pos) {
                    chunk = bp - pos;
                    saved = lines[i][pos + chunk];
                }
            }

            lines[i][pos + chunk] = '\0';
            shell_print(lines[i] + pos);
            shell_print("\n");
            lines[i][pos + chunk] = saved;

            while (pos < len2 && lines[i][pos] == ' ') pos++;
            if (pos >= len2) break;
        }
    }

    free_lines(lines, count);
    shell_last_exit_code = 0;
}

/* =========================================================================
 * cmd_expand  -  -t N (default 8), only leading tabs (-i)
 * ========================================================================= */
void cmd_expand(const char *args)
{
    if (!args || !*args) {
        shell_print("expand - convert tabs to spaces\n");
        shell_print("Usage: expand [-t N] [FILE]\n");
        shell_print("  -t N    Tab stop every N columns (default: 8)\n");
        shell_last_exit_code = 1;
        return;
    }

    int tab_width = 8;
    char *filename = NULL;

    const char *p = args;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;

        if (*p == '-') {
            if (p[1] == 't' && p[2] == ' ') {
                p += 3;
                char tbuf[16];
                int ti = 0;
                while (*p >= '0' && *p <= '9' && ti < 15) tbuf[ti++] = *p++;
                tbuf[ti] = '\0';
                tab_width = atoi(tbuf);
                if (tab_width <= 0) tab_width = 8;
            } else {
                while (*p && *p != ' ') p++;
            }
        } else {
            filename = (char *)p;
            while (*p && *p != ' ') p++;
            break;
        }
    }

    while (*filename == ' ') filename++;
    char *end = filename;
    while (*end && *end != ' ') end++;
    char fname[MAX_LINE];
    size_t len = end - filename;
    if (len >= MAX_LINE) len = MAX_LINE - 1;
    memcpy(fname, filename, len);
    fname[len] = '\0';

    if (!fname[0]) {
        shell_error("expand: missing file operand\n");
        shell_last_exit_code = 1;
        return;
    }

    file_t *file = NULL;
    int32_t err = vfs_open(fname, FILE_MODE_READ, &file);
    if (err < 0) {
        shell_error("expand: cannot open file: ");
        shell_error(fname);
        shell_error("\n");
        shell_last_exit_code = 1;
        return;
    }

    char line_buf[MAX_LINE * 4];
    char out_buf[MAX_LINE * 4];

    while (1) {
        int pos = 0;
        int done = 0;

        while (pos < (int)(sizeof(line_buf) - 1)) {
            char c;
            int32_t n = vfs_read(file, &c, 1);
            if (n <= 0) { done = 1; break; }
            if (c == '\n' || c == '\r') {
                line_buf[pos++] = c;
                if (c == '\r') {
                    char c2;
                    int32_t n2 = vfs_read(file, &c2, 1);
                    if (n2 > 0 && c2 != '\n') {
                        vfs_seek(file, -1, SEEK_CUR);
                    } else if (n2 > 0) {
                        line_buf[pos++] = c2;
                    }
                }
                break;
            }
            line_buf[pos++] = c;
        }
        line_buf[pos] = '\0';

        if (done && pos == 0) break;

        int out_pos = 0;
        int col = 0;

        for (int i = 0; line_buf[i] && line_buf[i] != '\n' && line_buf[i] != '\r'; i++) {
            if (line_buf[i] == '\t') {
                int spaces = tab_width - (col % tab_width);
                for (int s = 0; s < spaces && out_pos < (int)(sizeof(out_buf) - 1); s++) {
                    out_buf[out_pos++] = ' ';
                    col++;
                }
            } else {
                if (out_pos < (int)(sizeof(out_buf) - 1)) {
                    out_buf[out_pos++] = line_buf[i];
                    col++;
                }
            }
        }

        out_buf[out_pos] = '\0';
        shell_print(out_buf);
        shell_print("\n");
    }

    vfs_close(file);
    shell_last_exit_code = 0;
}

/* =========================================================================
 * cmd_unexpand  -  -t N (default 8), only leading spaces (-a implied)
 * ========================================================================= */
void cmd_unexpand(const char *args)
{
    if (!args || !*args) {
        shell_print("unexpand - convert spaces to tabs\n");
        shell_print("Usage: unexpand [-t N] [FILE]\n");
        shell_print("  -t N    Tab stop every N columns (default: 8)\n");
        shell_last_exit_code = 1;
        return;
    }

    int tab_width = 8;
    char *filename = NULL;

    const char *p = args;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;

        if (*p == '-') {
            if (p[1] == 't' && p[2] == ' ') {
                p += 3;
                char tbuf[16];
                int ti = 0;
                while (*p >= '0' && *p <= '9' && ti < 15) tbuf[ti++] = *p++;
                tbuf[ti] = '\0';
                tab_width = atoi(tbuf);
                if (tab_width <= 0) tab_width = 8;
            } else {
                while (*p && *p != ' ') p++;
            }
        } else {
            filename = (char *)p;
            while (*p && *p != ' ') p++;
            break;
        }
    }

    while (*filename == ' ') filename++;
    char *end = filename;
    while (*end && *end != ' ') end++;
    char fname[MAX_LINE];
    size_t len = end - filename;
    if (len >= MAX_LINE) len = MAX_LINE - 1;
    memcpy(fname, filename, len);
    fname[len] = '\0';

    if (!fname[0]) {
        shell_error("unexpand: missing file operand\n");
        shell_last_exit_code = 1;
        return;
    }

    char **lines = NULL;
    int count = load_lines(fname, &lines);
    if (count < 0) {
        shell_last_exit_code = 1;
        return;
    }

    for (int li = 0; li < count; li++) {
        char *line = lines[li];
        int len2 = 0;
        while (line[len2]) len2++;

        char out_buf[MAX_LINE * 2];
        int out_pos = 0;
        int col = 0;

        for (int i = 0; i < len2; i++) {
            if (line[i] != ' ') {
                out_buf[out_pos++] = line[i];
                col++;
            } else {
                int spaces = 0;
                int j = i;
                while (j < len2 && line[j] == ' ') {
                    spaces++;
                    j++;
                }

                int tabs = spaces / tab_width;
                int remaining = spaces % tab_width;

                if (tabs > 0 && col % tab_width == 0) {
                    for (int t = 0; t < tabs; t++) {
                        out_buf[out_pos++] = '\t';
                        col += tab_width;
                    }
                    col -= tab_width;
                }

                for (int s = 0; s < remaining; s++) {
                    out_buf[out_pos++] = ' ';
                    col++;
                }

                i = j - 1;
            }
        }

        out_buf[out_pos] = '\0';
        shell_print(out_buf);
        shell_print("\n");
    }

    free_lines(lines, count);
    shell_last_exit_code = 0;
}

/* =========================================================================
 * cmd_nl  -  number non-empty lines
 * ========================================================================= */
void cmd_nl(const char *file)
{
    if (!file || !*file) {
        shell_print("nl - number lines of a file\n");
        shell_print("Usage: nl [FILE]\n");
        shell_print("  Numbers all non-empty lines.\n");
        shell_last_exit_code = 1;
        return;
    }

    while (*file == ' ') file++;
    char *end = (char *)file;
    while (*end && *end != ' ') end++;
    char fname[MAX_LINE];
    size_t len = end - file;
    if (len >= MAX_LINE) len = MAX_LINE - 1;
    memcpy(fname, file, len);
    fname[len] = '\0';

    if (!fname[0]) {
        shell_error("nl: missing file operand\n");
        shell_last_exit_code = 1;
        return;
    }

    char **lines = NULL;
    int count = load_lines(fname, &lines);
    if (count < 0) {
        shell_last_exit_code = 1;
        return;
    }

    char num_buf[16];

    for (int i = 0; i < count; i++) {
        int len2 = 0;
        while (lines[i][len2] == ' ' || lines[i][len2] == '\t') len2++;

        if (lines[i][len2] == '\0') {
            shell_print("\n");
        } else {
            int num = i + 1;
            int pos = 0;
            num_buf[pos++] = '\t';
            int tmp = num;
            char rev[16];
            int rpos = 0;
            if (tmp == 0) rev[rpos++] = '0';
            while (tmp > 0) {
                rev[rpos++] = '0' + (tmp % 10);
                tmp /= 10;
            }
            while (rpos > 0) num_buf[pos++] = rev[--rpos];
            num_buf[pos] = '\0';
            shell_print(num_buf);
            shell_print(lines[i]);
            shell_print("\n");
        }
    }

    free_lines(lines, count);
    shell_last_exit_code = 0;
}

/* =========================================================================
 * cmd_look  -  prefix match search
 * ========================================================================= */
void cmd_look(const char *args)
{
    if (!args || !*args) {
        shell_print("look - display lines beginning with a given prefix\n");
        shell_print("Usage: look PREFIX [FILE]\n");
        shell_last_exit_code = 1;
        return;
    }

    char prefix[MAX_LINE];
    char *filename = NULL;

    const char *p = args;
    while (*p == ' ') p++;

    int pi = 0;
    while (*p && *p != ' ' && pi < MAX_LINE - 1) {
        prefix[pi++] = *p++;
    }
    prefix[pi] = '\0';

    while (*p == ' ') p++;
    if (*p) {
        filename = (char *)p;
        while (*p && *p != ' ') p++;
    }

    while (*filename == ' ') filename++;
    char *end = filename;
    while (*end && *end != ' ') end++;
    char fname[MAX_LINE];
    size_t len = end - filename;
    if (len >= MAX_LINE) len = MAX_LINE - 1;
    memcpy(fname, filename, len);
    fname[len] = '\0';

    if (!fname[0]) {
        shell_error("look: missing file operand\n");
        shell_last_exit_code = 1;
        return;
    }

    int prefix_len = 0;
    while (prefix[prefix_len]) prefix_len++;

    char **lines = NULL;
    int count = load_lines(fname, &lines);
    if (count < 0) {
        shell_last_exit_code = 1;
        return;
    }

    for (int i = 0; i < count; i++) {
        int line_len = 0;
        while (lines[i][line_len]) line_len++;

        if (line_len >= prefix_len) {
            int match = 1;
            for (int j = 0; j < prefix_len; j++) {
                if (lines[i][j] != prefix[j]) {
                    match = 0;
                    break;
                }
            }
            if (match) {
                shell_print(lines[i]);
                shell_print("\n");
            }
        }
    }

    free_lines(lines, count);
    shell_last_exit_code = 0;
}

/* =========================================================================
 * cmd_comm  -  compare two sorted files, output three columns
 * ========================================================================= */
static int line_cmp(const void *a, const void *b)
{
    const char *sa = *(const char **)a;
    const char *sb = *(const char **)b;
    while (*sa && *sb) {
        if (*sa != *sb) return (unsigned char)*sa - (unsigned char)*sb;
        sa++; sb++;
    }
    return (unsigned char)*sa - (unsigned char)*sb;
}

void cmd_comm(const char *args)
{
    if (!args || !*args) {
        shell_print("comm - compare two sorted files line by line\n");
        shell_print("Usage: comm FILE1 FILE2\n");
        shell_print("  Output: col1 (only FILE1), col2 (only FILE2), col3 (common).\n");
        shell_last_exit_code = 1;
        return;
    }

    const char *p = args;
    while (*p == ' ') p++;

    char file1[MAX_LINE], file2[MAX_LINE];
    file1[0] = file2[0] = '\0';

    int fi = 0;
    while (*p && *p != ' ') file1[fi++] = *p++;
    file1[fi] = '\0';

    while (*p == ' ') p++;
    fi = 0;
    while (*p && *p != ' ') file2[fi++] = *p++;
    file2[fi] = '\0';

    if (!file1[0] || !file2[0]) {
        shell_error("comm: missing file operand\n");
        shell_last_exit_code = 1;
        return;
    }

    char **lines1 = NULL;
    int count1 = load_lines(file1, &lines1);
    if (count1 < 0) {
        shell_last_exit_code = 1;
        return;
    }

    char **lines2 = NULL;
    int count2 = load_lines(file2, &lines2);
    if (count2 < 0) {
        free_lines(lines1, count1);
        shell_last_exit_code = 1;
        return;
    }

    qsort(lines1, count1, sizeof(char *), line_cmp);
    qsort(lines2, count2, sizeof(char *), line_cmp);

    int i = 0, j = 0;
    char tab1[2] = { '\t', '\0' };
    char tab2[3] = { '\t', '\t', '\0' };

    while (i < count1 && j < count2) {
        int cmp = line_cmp(&lines1[i], &lines2[j]);

        if (cmp == 0) {
            shell_print(tab2);
            shell_print(lines1[i]);
            shell_print("\n");
            i++;
            j++;
        } else if (cmp < 0) {
            shell_print(lines1[i]);
            shell_print("\n");
            i++;
        } else {
            shell_print(tab1);
            shell_print(lines2[j]);
            shell_print("\n");
            j++;
        }
    }

    while (i < count1) {
        shell_print(lines1[i]);
        shell_print("\n");
        i++;
    }

    while (j < count2) {
        shell_print(tab1);
        shell_print(lines2[j]);
        shell_print("\n");
        j++;
    }

    free_lines(lines1, count1);
    free_lines(lines2, count2);
    shell_last_exit_code = 0;
}

/* =========================================================================
 * cmd_tsort  -  topological sort (Kahn's algorithm)
 * Each line: "a b" means a precedes b
 * ========================================================================= */
void cmd_tsort(const char *file)
{
    if (!file || !*file) {
        shell_print("tsort - perform a topological sort\n");
        shell_print("Usage: tsort FILE\n");
        shell_print("  Each line contains two tokens: 'a b' means a precedes b.\n");
        shell_error("tsort: missing file operand\n");
        shell_last_exit_code = 1;
        return;
    }

    while (*file == ' ') file++;
    char *end = (char *)file;
    while (*end && *end != ' ') end++;
    char fname[MAX_LINE];
    size_t len = end - file;
    if (len >= MAX_LINE) len = MAX_LINE - 1;
    memcpy(fname, file, len);
    fname[len] = '\0';

    if (!fname[0]) {
        shell_error("tsort: missing file operand\n");
        shell_last_exit_code = 1;
        return;
    }

    char **lines = NULL;
    int count = load_lines(fname, &lines);
    if (count < 0) {
        shell_last_exit_code = 1;
        return;
    }

    /* Collect all unique nodes and edges */
    char nodes[256][MAX_LINE];
    int node_count = 0;
    int adj[256][256];
    int indeg[256];
    for (int i = 0; i < 256; i++) {
        indeg[i] = 0;
        for (int j = 0; j < 256; j++) adj[i][j] = 0;
    }

    /* Simple hash map: store nodes as strings */
    char node_map[256][MAX_LINE];
    int node_map_count = 0;

    for (int i = 0; i < count; i++) {
        char *line = lines[i];
        char token1[MAX_LINE], token2[MAX_LINE];
        int t1len = 0, t2len = 0;

        while (*line == ' ' || *line == '\t') line++;
        while (*line && *line != ' ' && *line != '\t' && t1len < MAX_LINE - 1) {
            token1[t1len++] = *line++;
        }
        token1[t1len] = '\0';

        while (*line == ' ' || *line == '\t') line++;
        while (*line && *line != ' ' && *line != '\t' && t2len < MAX_LINE - 1) {
            token2[t2len++] = *line++;
        }
        token2[t2len] = '\0';

        if (!t1len || !t2len) continue;

        /* Find or add node 1 */
        int n1 = -1, n2 = -1;
        for (int k = 0; k < node_map_count; k++) {
            if (strcmp(node_map[k], token1) == 0) { n1 = k; break; }
        }
        if (n1 < 0 && node_map_count < 256) {
            n1 = node_map_count++;
            strcpy(node_map[n1], token1);
        }

        for (int k = 0; k < node_map_count; k++) {
            if (strcmp(node_map[k], token2) == 0) { n2 = k; break; }
        }
        if (n2 < 0 && node_map_count < 256) {
            n2 = node_map_count++;
            strcpy(node_map[n2], token2);
        }

        if (n1 >= 0 && n2 >= 0 && n1 != n2) {
            int exists = 0;
            for (int k = 0; k < 256; k++) {
                if (adj[n1][k] == n2 + 1) { exists = 1; break; }
            }
            if (!exists) {
                for (int k = 0; k < 256; k++) {
                    if (adj[n1][k] == 0) { adj[n1][k] = n2 + 1; break; }
                }
                indeg[n2]++;
            }
        }
    }

    /* Kahn's algorithm */
    int queue[256];
    int qhead = 0, qtail = 0;

    for (int i = 0; i < node_map_count; i++) {
        if (indeg[i] == 0) queue[qtail++] = i;
    }

    char result[256][MAX_LINE];
    int result_count = 0;
    int visited_count = 0;
    int in_queue[256];
    for (int i = 0; i < 256; i++) in_queue[i] = 0;

    while (qhead < qtail) {
        int u = queue[qhead++];
        if (visited_count >= node_map_count) break;

        strcpy(result[result_count++], node_map[u]);
        in_queue[u] = 0;

        for (int k = 0; k < 256; k++) {
            if (adj[u][k] == 0) break;
            int v = adj[u][k] - 1;
            indeg[v]--;
            if (indeg[v] == 0 && !in_queue[v]) {
                queue[qtail++] = v;
                in_queue[v] = 1;
            }
        }
        visited_count++;
    }

    if (result_count < node_map_count && node_map_count > 0) {
        shell_error("tsort: cycle detected\n");
        free_lines(lines, count);
        shell_last_exit_code = 1;
        return;
    }

    for (int i = 0; i < result_count; i++) {
        shell_print(result[i]);
        shell_print("\n");
    }

    free_lines(lines, count);
    shell_last_exit_code = 0;
}
