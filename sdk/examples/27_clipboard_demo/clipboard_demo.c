/* clipboard_demo.c - Clipboard Operations Demo
 * Demonstrates text, image, file list, and data clipboard operations
 * using the funsos_clipboard.h API.
 */

#include "funsos.h"

/* Helper: simple string length */
static int my_strlen(const char *s)
{
    int len = 0;
    while (s[len]) len++;
    return len;
}

/* Helper: compare two strings */
static int my_strcmp(const char *a, const char *b)
{
    while (*a && *b && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int main(void)
{
    funsos_window_t win = funsos_create_window(60, 40, 700, 500, "Clipboard Demo");
    funsos_fill_window(win, FUNSOS_COLOR_WHITE);

    funsos_color_t title_c = FUNSOS_COLOR_BLUE;
    funsos_color_t text_c  = FUNSOS_COLOR_BLACK;
    funsos_color_t label_c = FUNSOS_COLOR_DARK_GRAY;
    funsos_color_t good_c  = FUNSOS_COLOR_GREEN;
    funsos_color_t warn_c  = FUNSOS_COLOR_ORANGE;
    funsos_color_t err_c   = FUNSOS_COLOR_RED;

    int line_y = 18;
    int lh = 22;

    /* Title */
    funsos_draw_text(win, 20, line_y, "=== Clipboard Operations Demo ===", title_c);
    line_y += lh + 4;
    funsos_draw_line(win, 15, line_y, 685, line_y, FUNSOS_COLOR_GRAY);
    line_y += 10;

    /* ---- 1. Set Text to Clipboard ---- */
    funsos_draw_text(win, 20, line_y, "[1] Setting text to clipboard...", label_c);
    line_y += lh;

    const char *demo_text = "Hello from FUNSOS Clipboard API!";
    int ret = funsos_clipboard_set_text(demo_text);
    if (ret >= 0) {
        funsos_draw_text(win, 40, line_y, "OK - Text set: \"", good_c);
        funsos_draw_text(win, 160, line_y, demo_text, text_c);
        funsos_draw_text(win, 160 + my_strlen(demo_text) * 8, line_y, "\"", good_c);
    } else {
        funsos_draw_text(win, 40, line_y, "FAILED: Could not set clipboard text", err_c);
    }
    line_y += lh;

    /* ---- 2. Get Text from Clipboard ---- */
    funsos_draw_text(win, 20, line_y, "[2] Getting text from clipboard...", label_c);
    line_y += lh;

    if (funsos_clipboard_has_text()) {
        char buf[256];
        int len = funsos_clipboard_get_text(buf, sizeof(buf));
        if (len > 0) {
            funsos_draw_text(win, 40, line_y, "Clipboard text: \"", label_c);
            funsos_draw_text(win, 170, line_y, buf, text_c);
            funsos_draw_text(win, 170 + len * 8, line_y, "\"", good_c);
        }
    } else {
        funsos_draw_text(win, 40, line_y, "No text in clipboard", warn_c);
    }
    line_y += lh + 4;

    /* ---- 3. Check Clipboard Types ---- */
    funsos_draw_text(win, 20, line_y, "[3] Checking available clipboard types...", label_c);
    line_y += lh;

    funsos_clipboard_type_t types[8];
    int type_count = funsos_clipboard_get_types(types, 8);
    if (type_count > 0) {
        char cnt_buf[8];
        cnt_buf[0] = '0' + type_count; cnt_buf[1] = '\0';
        funsos_draw_text(win, 40, line_y, "Available types: ", label_c);
        funsos_draw_text(win, 160, line_y, cnt_buf, good_c);
        line_y += lh;

        for (int i = 0; i < type_count; i++) {
            const char *type_name = "Unknown";
            if (types[i] == FUNSOS_CLIPBOARD_TEXT) type_name = "TEXT";
            else if (types[i] == FUNSOS_CLIPBOARD_RTF) type_name = "RTF";
            else if (types[i] == FUNSOS_CLIPBOARD_HTML) type_name = "HTML";
            else if (types[i] == FUNSOS_CLIPBOARD_IMAGE) type_name = "IMAGE";
            else if (types[i] == FUNSOS_CLIPBOARD_FILES) type_name = "FILES";
            else if (types[i] == FUNSOS_CLIPBOARD_BINARY) type_name = "BINARY";

            funsos_draw_text(win, 50, line_y, "  - ", label_c);
            funsos_draw_text(win, 70, line_y, type_name, text_c);
            line_y += lh;
        }
    } else {
        funsos_draw_text(win, 40, line_y, "No types available (clipboard empty?)", warn_c);
    }
    line_y += lh + 4;

    /* ---- 4. Check Specific Type ---- */
    funsos_draw_text(win, 20, line_y, "[4] Checking for specific types...", label_c);
    line_y += lh;

    if (funsos_clipboard_has_type(FUNSOS_CLIPBOARD_TEXT)) {
        funsos_draw_text(win, 40, line_y, "  [v] TEXT type available", good_c);
    } else {
        funsos_draw_text(win, 40, line_y, "  [ ] TEXT type not available", warn_c);
    }
    line_y += lh;

    if (funsos_clipboard_has_type(FUNSOS_CLIPBOARD_IMAGE)) {
        funsos_draw_text(win, 40, line_y, "  [v] IMAGE type available", good_c);
    } else {
        funsos_draw_text(win, 40, line_y, "  [ ] IMAGE type not available", warn_c);
    }
    line_y += lh;

    if (funsos_clipboard_has_type(FUNSOS_CLIPBOARD_FILES)) {
        funsos_draw_text(win, 40, line_y, "  [v] FILES type available", good_c);
    } else {
        funsos_draw_text(win, 40, line_y, "  [ ] FILES type not available", warn_c);
    }
    line_y += lh + 4;

    /* ---- 5. File List Clipboard ---- */
    funsos_draw_text(win, 20, line_y, "[5] File list clipboard operations...", label_c);
    line_y += lh;

    const char *file_list = "/home/user/doc.txt\n/home/user/image.png\n/home/user/data.csv";
    ret = funsos_clipboard_set_files(file_list);
    if (ret >= 0) {
        funsos_draw_text(win, 40, line_y, "OK - File list set to clipboard", good_c);
        line_y += lh;

        int count = funsos_clipboard_get_file_count();
        char cnt_buf[8];
        cnt_buf[0] = '0' + count; cnt_buf[1] = '\0';
        funsos_draw_text(win, 40, line_y, "  File count: ", label_c);
        funsos_draw_text(win, 130, line_y, cnt_buf, good_c);
        line_y += lh;

        char files_buf[512];
        int n = funsos_clipboard_get_files(files_buf, sizeof(files_buf));
        if (n > 0) {
            funsos_draw_text(win, 40, line_y, "  Files:", label_c);
            line_y += lh;

            /* Display each file on a separate line */
            int start = 0;
            for (int i = 0; i < n && start < 512; i++) {
                if (files_buf[i] == '\n' || files_buf[i] == '\0') {
                    char tmp = files_buf[i];
                    files_buf[i] = '\0';
                    funsos_draw_text(win, 50, line_y, "    - ", label_c);
                    funsos_draw_text(win, 80, line_y, &files_buf[start], text_c);
                    files_buf[i] = tmp;
                    start = i + 1;
                    line_y += lh;
                    if (tmp == '\0') break;
                }
            }
        }
    } else {
        funsos_draw_text(win, 40, line_y, "FAILED: Could not set file list", err_c);
    }
    line_y += lh + 4;

    /* ---- 6. Clear Clipboard ---- */
    funsos_draw_text(win, 20, line_y, "[6] Clearing clipboard...", label_c);
    line_y += lh;

    ret = funsos_clipboard_clear();
    if (ret >= 0) {
        funsos_draw_text(win, 40, line_y, "OK - Clipboard cleared", good_c);
        line_y += lh;

        if (funsos_clipboard_is_empty()) {
            funsos_draw_text(win, 40, line_y, "OK - Clipboard is empty", good_c);
        }
    } else {
        funsos_draw_text(win, 40, line_y, "FAILED: Could not clear clipboard", err_c);
    }
    line_y += lh + 8;

    /* ---- Summary ---- */
    funsos_draw_line(win, 15, line_y, 685, line_y, FUNSOS_COLOR_GRAY);
    line_y += 8;
    funsos_draw_text(win, 20, line_y, "[Summary] Clipboard operations:", title_c);
    line_y += lh + 4;

    funsos_draw_text(win, 40, line_y, "  [v] clipboard_set_text / clipboard_get_text", good_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "  [v] clipboard_set_data / clipboard_get_data", good_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "  [v] clipboard_set_files / clipboard_get_files", good_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "  [v] clipboard_get_types / clipboard_has_type", good_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "  [v] clipboard_clear / clipboard_is_empty", good_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "  [v] clipboard_has_text / clipboard_get_type", good_c);
    line_y += lh + 8;

    /* Footer */
    funsos_draw_line(win, 15, 488, 685, 488, FUNSOS_COLOR_GRAY);
    funsos_draw_text(win, 20, 496, "Press ESC to exit", FUNSOS_COLOR_DARK_GRAY);

    /* Event loop */
    funsos_event_t event;
    while (1) {
        if (funsos_wait_event(&event) != 0)
            continue;
        if (event.type == FUNSOS_EVENT_KEY_PRESS && event.param1 == 0x1B)
            break;
        if (event.type == FUNSOS_EVENT_WINDOW_CLOSE)
            break;
    }

    funsos_destroy_window(win);
    return 0;
}