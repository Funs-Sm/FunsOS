#include "ascii_box.h"
#include "string.h"
#include "stdio.h"

static int str_len(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

static int append_char(char *buf, int pos, char c) {
    buf[pos++] = c;
    return pos;
}

static int append_str(char *buf, int pos, const char *s) {
    while (*s) {
        buf[pos++] = *s++;
    }
    return pos;
}

static int append_n_chars(char *buf, int pos, char c, int n) {
    for (int i = 0; i < n; i++) {
        buf[pos++] = c;
    }
    return pos;
}

static int draw_box_generic(char *buf, int width, int height, const char *title,
                            char tl, char tr, char bl, char br, char h, char v,
                            int has_title_sep, char title_sep_left, char title_sep_right, char title_sep_h) {
    int pos = 0;
    int inner_w = width - 2;
    if (inner_w < 1) inner_w = 1;
    if (height < 2) height = 2;

    buf[pos++] = tl;
    if (title && *title && has_title_sep) {
        int title_len = str_len(title);
        if (title_len > inner_w - 2) title_len = inner_w - 2;
        int total_pad = inner_w - title_len - 2;
        if (total_pad < 0) total_pad = 0;
        int left_pad = total_pad / 2;
        int right_pad = total_pad - left_pad;
        pos = append_n_chars(buf, pos, h, left_pad);
        pos = append_char(buf, pos, ' ');
        for (int i = 0; i < title_len; i++) {
            buf[pos++] = title[i];
        }
        pos = append_char(buf, pos, ' ');
        pos = append_n_chars(buf, pos, h, right_pad);
    } else {
        pos = append_n_chars(buf, pos, h, inner_w);
    }
    buf[pos++] = tr;
    buf[pos++] = '\n';

    int body_lines = height - 2;
    for (int i = 0; i < body_lines; i++) {
        buf[pos++] = v;
        pos = append_n_chars(buf, pos, ' ', inner_w);
        buf[pos++] = v;
        buf[pos++] = '\n';
    }

    buf[pos++] = bl;
    pos = append_n_chars(buf, pos, h, inner_w);
    buf[pos++] = br;
    buf[pos++] = '\n';

    buf[pos] = '\0';
    return pos;
}

int ascii_draw_box(char *buf, int width, int height, const char *title) {
    return draw_box_generic(buf, width, height, title,
                            '+', '+', '+', '+', '-', '|',
                            1, '+', '+', '-');
}

int ascii_draw_double_box(char *buf, int width, int height, const char *title) {
    return draw_box_generic(buf, width, height, title,
                            '#', '#', '#', '#', '=', '#',
                            1, '#', '#', '=');
}

int ascii_draw_separator(char *buf, int width) {
    int pos = 0;
    if (width < 2) width = 2;
    buf[pos++] = '+';
    for (int i = 0; i < width - 2; i++) {
        buf[pos++] = '-';
    }
    buf[pos++] = '+';
    buf[pos++] = '\n';
    buf[pos] = '\0';
    return pos;
}

int ascii_draw_header(char *buf, int width, const char *text) {
    int pos = 0;
    int inner_w = width - 2;
    if (inner_w < 1) inner_w = 1;
    if (width < 2) width = 2;

    buf[pos++] = '+';
    pos = append_n_chars(buf, pos, '-', inner_w);
    buf[pos++] = '+';
    buf[pos++] = '\n';

    buf[pos++] = '|';
    int text_len = str_len(text);
    if (text_len > inner_w) text_len = inner_w;
    int total_pad = inner_w - text_len;
    if (total_pad < 0) total_pad = 0;
    int left_pad = total_pad / 2;
    int right_pad = total_pad - left_pad;
    pos = append_n_chars(buf, pos, ' ', left_pad);
    for (int i = 0; i < text_len; i++) {
        buf[pos++] = text[i];
    }
    pos = append_n_chars(buf, pos, ' ', right_pad);
    buf[pos++] = '|';
    buf[pos++] = '\n';

    buf[pos++] = '+';
    pos = append_n_chars(buf, pos, '-', inner_w);
    buf[pos++] = '+';
    buf[pos++] = '\n';

    buf[pos] = '\0';
    return pos;
}

int ascii_format_table(char *buf, int col_count, int *col_widths, char **headers, int row_count, char ***rows) {
    int pos = 0;
    int total_width = 1;

    for (int i = 0; i < col_count; i++) {
        total_width += col_widths[i] + 1;
    }

    buf[pos++] = '+';
    for (int i = 0; i < col_count; i++) {
        for (int j = 0; j < col_widths[i]; j++) {
            buf[pos++] = '-';
        }
        buf[pos++] = '+';
    }
    buf[pos++] = '\n';

    buf[pos++] = '|';
    for (int i = 0; i < col_count; i++) {
        const char *h = headers[i] ? headers[i] : "";
        int hlen = str_len(h);
        int cw = col_widths[i];
        if (hlen > cw) hlen = cw;
        int pad = cw - hlen;
        for (int j = 0; j < hlen; j++) {
            buf[pos++] = h[j];
        }
        for (int j = 0; j < pad; j++) {
            buf[pos++] = ' ';
        }
        buf[pos++] = '|';
    }
    buf[pos++] = '\n';

    buf[pos++] = '+';
    for (int i = 0; i < col_count; i++) {
        for (int j = 0; j < col_widths[i]; j++) {
            buf[pos++] = '=';
        }
        buf[pos++] = '+';
    }
    buf[pos++] = '\n';

    for (int r = 0; r < row_count; r++) {
        buf[pos++] = '|';
        for (int i = 0; i < col_count; i++) {
            const char *cell = (rows[r] && rows[r][i]) ? rows[r][i] : "";
            int clen = str_len(cell);
            int cw = col_widths[i];
            if (clen > cw) clen = cw;
            int pad = cw - clen;
            for (int j = 0; j < clen; j++) {
                buf[pos++] = cell[j];
            }
            for (int j = 0; j < pad; j++) {
                buf[pos++] = ' ';
            }
            buf[pos++] = '|';
        }
        buf[pos++] = '\n';
    }

    buf[pos++] = '+';
    for (int i = 0; i < col_count; i++) {
        for (int j = 0; j < col_widths[i]; j++) {
            buf[pos++] = '-';
        }
        buf[pos++] = '+';
    }
    buf[pos++] = '\n';

    buf[pos] = '\0';
    return pos;
}

int ascii_progress_bar(char *buf, int width, int percent) {
    int pos = 0;
    int bar_width = width - 2;
    if (bar_width < 1) bar_width = 1;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    int filled = (bar_width * percent) / 100;
    int empty = bar_width - filled;

    buf[pos++] = '[';
    for (int i = 0; i < filled; i++) {
        buf[pos++] = '#';
    }
    for (int i = 0; i < empty; i++) {
        buf[pos++] = '-';
    }
    buf[pos++] = ']';

    char pct_buf[16];
    int pct_len = snprintf(pct_buf, sizeof(pct_buf), " %d%%", percent);
    for (int i = 0; i < pct_len; i++) {
        buf[pos++] = pct_buf[i];
    }

    buf[pos++] = '\n';
    buf[pos] = '\0';
    return pos;
}
