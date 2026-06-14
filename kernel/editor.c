#include "editor.h"
#include "vga_text.h"
#include "fb_console.h"
#include "keyboard.h"
#include "string.h"
#include "stdio.h"
#include "kheap.h"
#include "vfs.h"
#include "version.h"

/* Editor configuration */
#define EDITOR_MAX_LINES    1024
#define EDITOR_LINE_LEN     256
#define EDITOR_TAB_SIZE     4

/* External state from shell */
extern int vbe_mode_active_from_shell;
/* We'll use a local flag */
static int editor_vbe __attribute__((unused)) = 0;

/* Editor state */
static char *editor_lines[EDITOR_MAX_LINES];
static int editor_line_count = 0;
static char editor_filename[256] = "";
static int editor_modified = 0;

/* Cursor position */
static int editor_cur_line = 0;
static int editor_cur_col = 0;

/* Scroll offset */
static int editor_scroll_row = 0;
static int editor_scroll_col = 0;

/* Screen dimensions */
#define EDITOR_SCREEN_ROWS 24  /* 25 lines - 1 for status bar */
#define EDITOR_SCREEN_COLS 80

/* VGA color constants for syntax highlighting */
#define ED_COLOR_DEFAULT    0x07  /* Light grey on black */
#define ED_COLOR_KEYWORD    0x0B  /* Light cyan on black */
#define ED_COLOR_PREPROC    0x0C  /* Light red on black */
#define ED_COLOR_STRING     0x0D  /* Light magenta on black */
#define ED_COLOR_COMMENT    0x02  /* Green on black */
#define ED_COLOR_NUMBER     0x0E  /* Yellow on black */
#define ED_COLOR_STATUS     0x1F  /* White on blue */
#define ED_COLOR_LINENUM    0x08  /* Dark grey on black */

/* C keywords for syntax highlighting */
static const char *c_keywords[] = {
    "int", "char", "void", "if", "else", "for", "while", "return",
    "struct", "typedef", "switch", "case", "break", "continue",
    "do", "goto", "static", "extern", "const", "unsigned",
    "signed", "long", "short", "float", "double", "enum",
    "union", "volatile", "register", "auto", "sizeof",
    NULL
};

static const char *c_preproc[] = {
    "#include", "#define", "#ifdef", "#ifndef", "#endif",
    "#if", "#else", "#elif", "#undef", "#pragma",
    NULL
};

/* ---- VGA direct buffer access ---- */
static uint16_t *vga_buffer = (uint16_t *)0xB8000;

static void editor_putc(int row, int col, char c, uint8_t color) {
    if (row < 0 || row >= 25 || col < 0 || col >= 80) return;
    vga_buffer[row * 80 + col] = (uint16_t)((color << 8) | c);
}

static void editor_clear_screen(void) {
    for (int i = 0; i < 80 * 25; i++) {
        vga_buffer[i] = (uint16_t)(0x0720); /* space with default color */
    }
}

/* ---- Line management ---- */

static void editor_init_lines(void) {
    for (int i = 0; i < EDITOR_MAX_LINES; i++) {
        editor_lines[i] = 0;
    }
    editor_line_count = 0;
    editor_cur_line = 0;
    editor_cur_col = 0;
    editor_scroll_row = 0;
    editor_scroll_col = 0;
    editor_modified = 0;
}

static void editor_add_line(int at, const char *text) {
    if (editor_line_count >= EDITOR_MAX_LINES) return;

    /* Shift lines down */
    for (int i = editor_line_count; i > at; i--) {
        editor_lines[i] = editor_lines[i - 1];
    }

    uint32_t len = 0;
    while (text && text[len]) len++;

    editor_lines[at] = (char *)kmalloc(EDITOR_LINE_LEN);
    if (!editor_lines[at]) return;

    if (text) {
        uint32_t j;
        for (j = 0; j < len && j < EDITOR_LINE_LEN - 1; j++) {
            editor_lines[at][j] = text[j];
        }
        editor_lines[at][j] = '\0';
    } else {
        editor_lines[at][0] = '\0';
    }

    editor_line_count++;
}

static void editor_free_lines(void) __attribute__((unused));
static void editor_free_lines(void) {
    for (int i = 0; i < editor_line_count; i++) {
        if (editor_lines[i]) {
            kfree(editor_lines[i]);
            editor_lines[i] = 0;
        }
    }
    editor_line_count = 0;
}

/* ---- Syntax highlighting ---- */

static int is_keyword(const char *word, uint32_t len) {
    for (int i = 0; c_keywords[i]; i++) {
        uint32_t klen = 0;
        while (c_keywords[i][klen]) klen++;
        if (len == klen && memcmp(word, c_keywords[i], len) == 0) return 1;
    }
    return 0;
}

static int is_preproc(const char *word, uint32_t len) {
    for (int i = 0; c_preproc[i]; i++) {
        uint32_t klen = 0;
        while (c_preproc[i][klen]) klen++;
        if (len == klen && memcmp(word, c_preproc[i], len) == 0) return 1;
    }
    return 0;
}

/* Determine the color for a character at position col in a line */
static uint8_t editor_get_color(const char *line, int col) {
    if (!line || col < 0) return ED_COLOR_DEFAULT;

    /* Check if we're in a string literal */
    int in_string = 0;
    int in_char = 0;
    int in_comment = 0;
    int in_line_comment = 0;
    int i = 0;

    while (i < col && line[i]) {
        if (in_line_comment) {
            i++;
            continue;
        }
        if (in_comment) {
            if (line[i] == '*' && line[i + 1] == '/') {
                in_comment = 0;
                i += 2;
                continue;
            }
            i++;
            continue;
        }
        if (in_string) {
            if (line[i] == '\\' && line[i + 1]) {
                i += 2;
                continue;
            }
            if (line[i] == '"') {
                in_string = 0;
                i++;
                continue;
            }
            i++;
            continue;
        }
        if (in_char) {
            if (line[i] == '\\' && line[i + 1]) {
                i += 2;
                continue;
            }
            if (line[i] == '\'') {
                in_char = 0;
                i++;
                continue;
            }
            i++;
            continue;
        }
        if (line[i] == '/' && line[i + 1] == '/') {
            in_line_comment = 1;
            i += 2;
            continue;
        }
        if (line[i] == '/' && line[i + 1] == '*') {
            in_comment = 1;
            i += 2;
            continue;
        }
        if (line[i] == '"') {
            in_string = 1;
            i++;
            continue;
        }
        if (line[i] == '\'') {
            in_char = 1;
            i++;
            continue;
        }
        i++;
    }

    if (in_line_comment || in_comment) return ED_COLOR_COMMENT;
    if (in_string || in_char) return ED_COLOR_STRING;

    /* Check if we're in a preprocessor directive */
    int first_non_space = 1;
    for (int j = 0; j <= col && line[j]; j++) {
        if (line[j] != ' ' && line[j] != '\t') {
            first_non_space = 0;
            break;
        }
    }
    if (first_non_space && line[0] == '#') return ED_COLOR_PREPROC;

    /* Check if current position is part of a keyword */
    if (line[col] == '#') return ED_COLOR_PREPROC;

    /* Find word boundaries around col */
    int word_start = col;
    int word_end = col;
    while (word_start > 0 &&
           ((line[word_start - 1] >= 'a' && line[word_start - 1] <= 'z') ||
            (line[word_start - 1] >= 'A' && line[word_start - 1] <= 'Z') ||
            (line[word_start - 1] >= '0' && line[word_start - 1] <= '9') ||
            line[word_start - 1] == '_')) {
        word_start--;
    }
    while (line[word_end] &&
           ((line[word_end] >= 'a' && line[word_end] <= 'z') ||
            (line[word_end] >= 'A' && line[word_end] <= 'Z') ||
            (line[word_end] >= '0' && line[word_end] <= '9') ||
            line[word_end] == '_')) {
        word_end++;
    }

    /* Check if col is actually part of a word character */
    char c = line[col];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '_')) {
        return ED_COLOR_DEFAULT;
    }

    uint32_t wlen = word_end - word_start;
    if (is_keyword(line + word_start, wlen)) return ED_COLOR_KEYWORD;
    if (is_preproc(line + word_start, wlen)) return ED_COLOR_PREPROC;

    /* Numbers */
    if (c >= '0' && c <= '9') return ED_COLOR_NUMBER;

    return ED_COLOR_DEFAULT;
}

/* ---- Rendering ---- */

static void editor_draw_status_bar(void) {
    char status[80];
    int len;

    /* Build status text */
    len = snprintf(status, sizeof(status),
                   " %s%s  L%d C%d  %s",
                   editor_filename[0] ? editor_filename : "[No Name]",
                   editor_modified ? " [modified]" : "",
                   editor_cur_line + 1,
                   editor_cur_col + 1,
                   KERNEL_STRING);

    /* Pad with spaces and draw */
    for (int i = 0; i < 80; i++) {
        char c = (i < len) ? status[i] : ' ';
        editor_putc(24, i, c, ED_COLOR_STATUS);
    }
}

static void editor_draw_line(int screen_row, int line_idx) {
    char *line = editor_lines[line_idx];
    int line_len = 0;
    if (line) {
        while (line[line_len]) line_len++;
    }

    /* Draw line number */
    char lnum[8];
    snprintf(lnum, sizeof(lnum), "%4d ", line_idx + 1);
    for (int i = 0; i < 5; i++) {
        editor_putc(screen_row, i, lnum[i], ED_COLOR_LINENUM);
    }

    /* Draw line content with syntax highlighting */
    for (int col = 0; col < EDITOR_SCREEN_COLS - 5; col++) {
        int actual_col = col + editor_scroll_col;
        char c;
        uint8_t color;

        if (actual_col < line_len && line) {
            c = line[actual_col];
            if (c == '\t') c = ' ';
            color = editor_get_color(line, actual_col);
        } else {
            c = ' ';
            color = ED_COLOR_DEFAULT;
        }

        editor_putc(screen_row, col + 5, c, color);
    }
}

static void editor_draw_screen(void) {
    editor_clear_screen();

    /* Draw text area */
    for (int row = 0; row < EDITOR_SCREEN_ROWS; row++) {
        int line_idx = row + editor_scroll_row;
        if (line_idx < editor_line_count) {
            editor_draw_line(row, line_idx);
        } else {
            /* Empty line - just draw line number area with ~ */
            editor_putc(row, 0, '~', ED_COLOR_LINENUM);
            for (int i = 1; i < 5; i++) {
                editor_putc(row, i, ' ', ED_COLOR_LINENUM);
            }
            for (int i = 5; i < 80; i++) {
                editor_putc(row, i, ' ', ED_COLOR_DEFAULT);
            }
        }
    }

    /* Draw status bar */
    editor_draw_status_bar();

    /* Position cursor */
    int screen_row = editor_cur_line - editor_scroll_row;
    int screen_col = editor_cur_col - editor_scroll_col + 5; /* +5 for line numbers */
    if (screen_row >= 0 && screen_row < 25 && screen_col >= 0 && screen_col < 80) {
        vga_text_set_cursor(screen_row, screen_col);
    }
}

/* ---- Scrolling ---- */

static void editor_ensure_cursor_visible(void) {
    if (editor_cur_line < editor_scroll_row) {
        editor_scroll_row = editor_cur_line;
    }
    if (editor_cur_line >= editor_scroll_row + EDITOR_SCREEN_ROWS) {
        editor_scroll_row = editor_cur_line - EDITOR_SCREEN_ROWS + 1;
    }
    if (editor_cur_col < editor_scroll_col) {
        editor_scroll_col = editor_cur_col;
    }
    if (editor_cur_col >= editor_scroll_col + EDITOR_SCREEN_COLS - 5) {
        editor_scroll_col = editor_cur_col - EDITOR_SCREEN_COLS + 6;
    }
}

/* ---- File operations ---- */

static void editor_load_file(const char *filename) {
    file_t *f = 0;
    if (vfs_open(filename, FILE_MODE_READ, &f) != 0 || !f) {
        /* File doesn't exist - start with empty buffer */
        editor_add_line(0, "");
        return;
    }

    char buf[512];
    int32_t nr;
    int buf_pos = 0;

    while ((nr = vfs_read(f, buf + buf_pos, 511 - buf_pos)) > 0) {
        buf_pos += nr;
        buf[buf_pos] = '\0';

        char *line_start = buf;
        char *newline;
        while ((newline = strchr(line_start, '\n')) != 0) {
            *newline = '\0';
            /* Remove trailing \r if present */
            uint32_t ll = 0;
            while (line_start[ll]) ll++;
            if (ll > 0 && line_start[ll - 1] == '\r') {
                line_start[ll - 1] = '\0';
            }
            editor_add_line(editor_line_count, line_start);
            line_start = newline + 1;
        }

        /* Move remaining partial line to beginning */
        uint32_t remaining = buf_pos - (line_start - buf);
        memmove(buf, line_start, remaining);
        buf_pos = remaining;
    }

    /* Handle last partial line */
    if (buf_pos > 0) {
        buf[buf_pos] = '\0';
        uint32_t ll = 0;
        while (buf[ll]) ll++;
        if (ll > 0 && buf[ll - 1] == '\r') {
            buf[ll - 1] = '\0';
        }
        editor_add_line(editor_line_count, buf);
    }

    if (editor_line_count == 0) {
        editor_add_line(0, "");
    }

    vfs_close(f);
}

static void editor_save_file(void) {
    if (!editor_filename[0]) return;

    file_t *f = 0;
    if (vfs_open(editor_filename, FILE_MODE_WRITE, &f) != 0 || !f) {
        return;
    }

    for (int i = 0; i < editor_line_count; i++) {
        if (editor_lines[i]) {
            uint32_t len = 0;
            while (editor_lines[i][len]) len++;
            vfs_write(f, editor_lines[i], len);
        }
        if (i < editor_line_count - 1) {
            vfs_write(f, "\n", 1);
        }
    }

    vfs_close(f);
    editor_modified = 0;
}

/* ---- Editing operations ---- */

static void editor_insert_char(char c) {
    if (editor_cur_line >= editor_line_count) return;
    char *line = editor_lines[editor_cur_line];
    if (!line) return;

    int len = 0;
    while (line[len]) len++;

    if (c == '\t') {
        /* Insert tab as spaces */
        int spaces = EDITOR_TAB_SIZE - (editor_cur_col % EDITOR_TAB_SIZE);
        for (int i = 0; i < spaces; i++) {
            if (len + 1 < EDITOR_LINE_LEN) {
                memmove(line + editor_cur_col + 1, line + editor_cur_col, len - editor_cur_col + 1);
                line[editor_cur_col] = ' ';
                editor_cur_col++;
                len++;
            }
        }
        editor_modified = 1;
        return;
    }

    if (len + 1 < EDITOR_LINE_LEN) {
        memmove(line + editor_cur_col + 1, line + editor_cur_col, len - editor_cur_col + 1);
        line[editor_cur_col] = c;
        editor_cur_col++;
        editor_modified = 1;
    }
}

static void editor_insert_newline(void) {
    if (editor_cur_line >= editor_line_count) return;
    char *line = editor_lines[editor_cur_line];
    if (!line) return;

    int len = 0;
    while (line[len]) len++;

    /* Split line at cursor */
    char *new_line = (char *)kmalloc(EDITOR_LINE_LEN);
    if (!new_line) return;

    uint32_t j;
    for (j = 0; line[editor_cur_col + j] && j < EDITOR_LINE_LEN - 1; j++) {
        new_line[j] = line[editor_cur_col + j];
    }
    new_line[j] = '\0';

    line[editor_cur_col] = '\0';

    editor_add_line(editor_cur_line + 1, new_line);
    kfree(new_line);

    editor_cur_line++;
    editor_cur_col = 0;
    editor_modified = 1;
}

static void editor_delete_char(void) {
    if (editor_cur_line >= editor_line_count) return;
    char *line = editor_lines[editor_cur_line];
    if (!line) return;

    if (editor_cur_col > 0) {
        /* Delete character before cursor */
        int len = 0;
        while (line[len]) len++;
        memmove(line + editor_cur_col - 1, line + editor_cur_col, len - editor_cur_col + 1);
        editor_cur_col--;
        editor_modified = 1;
    } else if (editor_cur_line > 0) {
        /* Join with previous line */
        char *prev_line = editor_lines[editor_cur_line - 1];
        if (prev_line) {
            int prev_len = 0;
            while (prev_line[prev_len]) prev_len++;
            editor_cur_col = prev_len;

            /* Append current line to previous */
            int cur_len = 0;
            while (line[cur_len]) cur_len++;
            for (int i = 0; i < cur_len && prev_len + i < EDITOR_LINE_LEN - 1; i++) {
                prev_line[prev_len + i] = line[i];
            }
            prev_line[prev_len + cur_len] = '\0';

            /* Remove current line */
            kfree(editor_lines[editor_cur_line]);
            for (int i = editor_cur_line; i < editor_line_count - 1; i++) {
                editor_lines[i] = editor_lines[i + 1];
            }
            editor_lines[editor_line_count - 1] = 0;
            editor_line_count--;

            editor_cur_line--;
            editor_modified = 1;
        }
    }
}

static void editor_delete_char_forward(void) {
    if (editor_cur_line >= editor_line_count) return;
    char *line = editor_lines[editor_cur_line];
    if (!line) return;

    int len = 0;
    while (line[len]) len++;

    if (editor_cur_col < len) {
        memmove(line + editor_cur_col, line + editor_cur_col + 1, len - editor_cur_col);
        editor_modified = 1;
    } else if (editor_cur_line < editor_line_count - 1) {
        /* Join with next line */
        char *next_line = editor_lines[editor_cur_line + 1];
        if (next_line) {
            int next_len = 0;
            while (next_line[next_len]) next_len++;
            for (int i = 0; i < next_len && len + i < EDITOR_LINE_LEN - 1; i++) {
                line[len + i] = next_line[i];
            }
            line[len + next_len] = '\0';

            kfree(editor_lines[editor_cur_line + 1]);
            for (int i = editor_cur_line + 1; i < editor_line_count - 1; i++) {
                editor_lines[i] = editor_lines[i + 1];
            }
            editor_lines[editor_line_count - 1] = 0;
            editor_line_count--;
            editor_modified = 1;
        }
    }
}

/* ---- Public API ---- */

void editor_open(const char *filename) {
    editor_init_lines();
    if (filename) {
        strncpy(editor_filename, filename, 255);
        editor_filename[255] = '\0';
        editor_load_file(editor_filename);
    } else {
        editor_filename[0] = '\0';
        editor_add_line(0, "");
    }
}

void editor_run(void) {
    editor_ensure_cursor_visible();
    editor_draw_screen();

    while (1) {
        /* Wait for keyboard input */
        while (!keyboard_has_data()) {
            asm volatile("hlt");
        }

        keyboard_event_t event;
        if (!keyboard_get_event(&event)) continue;
        if (!(event.flags & KEY_PRESSED)) continue;

        uint8_t ctrl = event.flags & KEY_CTRL;

        /* Ctrl+Q or Escape to exit */
        if ((ctrl && (event.ascii == 'q' || event.ascii == 'Q')) || event.scancode == 0x01) {
            /* Escape key scancode */
            break;
        }

        /* Ctrl+S to save */
        if (ctrl && (event.ascii == 's' || event.ascii == 'S')) {
            editor_save_file();
            editor_draw_screen();
            continue;
        }

        /* Arrow keys / WASD for movement */
        if (event.flags & KEY_EXTENDED) {
            /* Extended keys - scancode based */
            switch (event.scancode) {
                case 0x48: /* Up arrow */
                    if (editor_cur_line > 0) {
                        editor_cur_line--;
                        /* Clamp column to line length */
                        if (editor_lines[editor_cur_line]) {
                            int ll = 0;
                            while (editor_lines[editor_cur_line][ll]) ll++;
                            if (editor_cur_col > ll) editor_cur_col = ll;
                        }
                    }
                    break;
                case 0x50: /* Down arrow */
                    if (editor_cur_line < editor_line_count - 1) {
                        editor_cur_line++;
                        if (editor_lines[editor_cur_line]) {
                            int ll = 0;
                            while (editor_lines[editor_cur_line][ll]) ll++;
                            if (editor_cur_col > ll) editor_cur_col = ll;
                        }
                    }
                    break;
                case 0x4B: /* Left arrow */
                    if (editor_cur_col > 0) {
                        editor_cur_col--;
                    } else if (editor_cur_line > 0) {
                        editor_cur_line--;
                        if (editor_lines[editor_cur_line]) {
                            int ll = 0;
                            while (editor_lines[editor_cur_line][ll]) ll++;
                            editor_cur_col = ll;
                        }
                    }
                    break;
                case 0x4D: /* Right arrow */
                    if (editor_lines[editor_cur_line]) {
                        int ll = 0;
                        while (editor_lines[editor_cur_line][ll]) ll++;
                        if (editor_cur_col < ll) {
                            editor_cur_col++;
                        } else if (editor_cur_line < editor_line_count - 1) {
                            editor_cur_line++;
                            editor_cur_col = 0;
                        }
                    }
                    break;
                case 0x49: /* Page Up */
                    editor_cur_line -= EDITOR_SCREEN_ROWS;
                    if (editor_cur_line < 0) editor_cur_line = 0;
                    if (editor_lines[editor_cur_line]) {
                        int ll = 0;
                        while (editor_lines[editor_cur_line][ll]) ll++;
                        if (editor_cur_col > ll) editor_cur_col = ll;
                    }
                    break;
                case 0x51: /* Page Down */
                    editor_cur_line += EDITOR_SCREEN_ROWS;
                    if (editor_cur_line >= editor_line_count) editor_cur_line = editor_line_count - 1;
                    if (editor_lines[editor_cur_line]) {
                        int ll = 0;
                        while (editor_lines[editor_cur_line][ll]) ll++;
                        if (editor_cur_col > ll) editor_cur_col = ll;
                    }
                    break;
                case 0x47: /* Home */
                    editor_cur_col = 0;
                    break;
                case 0x4F: /* End */
                    if (editor_lines[editor_cur_line]) {
                        int ll = 0;
                        while (editor_lines[editor_cur_line][ll]) ll++;
                        editor_cur_col = ll;
                    }
                    break;
            }
            editor_ensure_cursor_visible();
            editor_draw_screen();
            continue;
        }

        /* WASD movement when Ctrl is held */
        if (ctrl) {
            switch (event.ascii) {
                case 'w': case 'W':
                    if (editor_cur_line > 0) editor_cur_line--;
                    break;
                case 's': case 'S':
                    /* Already handled as save above */
                    break;
                case 'a': case 'A':
                    if (editor_cur_col > 0) editor_cur_col--;
                    break;
                case 'd': case 'D':
                    if (editor_lines[editor_cur_line]) {
                        int ll = 0;
                        while (editor_lines[editor_cur_line][ll]) ll++;
                        if (editor_cur_col < ll) editor_cur_col++;
                    }
                    break;
                default:
                    continue;
            }
            editor_ensure_cursor_visible();
            editor_draw_screen();
            continue;
        }

        /* Enter - insert newline */
        if (event.ascii == '\n') {
            editor_insert_newline();
            editor_ensure_cursor_visible();
            editor_draw_screen();
            continue;
        }

        /* Backspace */
        if (event.ascii == '\b') {
            editor_delete_char();
            editor_ensure_cursor_visible();
            editor_draw_screen();
            continue;
        }

        /* Delete key (scancode 0x53) */
        if (event.scancode == 0x53) {
            editor_delete_char_forward();
            editor_ensure_cursor_visible();
            editor_draw_screen();
            continue;
        }

        /* Regular printable character */
        if (event.ascii >= 32 && event.ascii < 127) {
            editor_insert_char(event.ascii);
            editor_ensure_cursor_visible();
            editor_draw_screen();
            continue;
        }
    }

    /* Restore screen - clear and let shell redraw */
    vga_text_clear();
}
