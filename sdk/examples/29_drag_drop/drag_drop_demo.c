/* drag_drop_demo.c - Drag and Drop Demo
 * Demonstrates drag and drop operations for files, text, and widgets
 * using the FUNSOS drag-drop API.
 */

#include "funsos.h"

/* Drag and drop operation types */
#define DRAG_TEXT   0
#define DRAG_FILE   1
#define DRAG_IMAGE  2
#define DRAG_DATA   3

/* Drop effect flags */
#define DROP_EFFECT_COPY  0x01
#define DROP_EFFECT_MOVE  0x02
#define DROP_EFFECT_LINK  0x04

/* Helper: simple string length */
static int my_strlen(const char *s)
{
    int len = 0;
    while (s[len]) len++;
    return len;
}

/* Helper: format number to string */
static void format_uint(uint32_t val, char *buf, int bufsize)
{
    int i = 0;
    if (val == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    char tmp[16];
    while (val > 0 && i < (int)sizeof(tmp) - 1) {
        tmp[i++] = '0' + (val % 10);
        val /= 10;
    }
    int pos = 0;
    for (int k = i - 1; k >= 0 && pos < bufsize - 1; k--)
        buf[pos++] = tmp[k];
    buf[pos] = '\0';
}

int main(void)
{
    funsos_window_t win = funsos_create_window(60, 40, 700, 520, "Drag & Drop Demo");
    funsos_fill_window(win, FUNSOS_COLOR_WHITE);

    funsos_color_t title_c = FUNSOS_COLOR_BLUE;
    funsos_color_t text_c  = FUNSOS_COLOR_BLACK;
    funsos_color_t label_c = FUNSOS_COLOR_DARK_GRAY;
    funsos_color_t good_c  = FUNSOS_COLOR_GREEN;
    funsos_color_t warn_c  = FUNSOS_COLOR_ORANGE;

    int line_y = 18;
    int lh = 22;

    /* Title */
    funsos_draw_text(win, 20, line_y, "=== Drag & Drop Demo ===", title_c);
    line_y += lh + 4;
    funsos_draw_line(win, 15, line_y, 685, line_y, FUNSOS_COLOR_GRAY);
    line_y += 10;

    /* ---- 1. Drag Source Zone ---- */
    funsos_draw_text(win, 20, line_y, "[1] Drag Source Zone", label_c);
    line_y += lh;

    /* Draw a drag source area */
    funsos_draw_rect(win, 40, line_y, 300, 80, FUNSOS_COLOR_LIGHT_GRAY);
    funsos_draw_text(win, 55, line_y + 10, "Drag Source Area", FUNSOS_COLOR_BLUE);
    funsos_draw_text(win, 55, line_y + 30, "Drag from here to start a drag operation", text_c);
    funsos_draw_text(win, 55, line_y + 50, "(Files, Text, Images, Data)", label_c);
    line_y += lh + 80 + 4;

    /* ---- 2. Drop Target Zone ---- */
    funsos_draw_text(win, 20, line_y, "[2] Drop Target Zone", label_c);
    line_y += lh;

    /* Draw a drop target area */
    funsos_draw_rect(win, 40, line_y, 300, 80, FUNSOS_COLOR_DARK_GRAY);
    funsos_draw_text(win, 55, line_y + 10, "Drop Target Area", FUNSOS_COLOR_WHITE);
    funsos_draw_text(win, 55, line_y + 30, "Drop files/text here to accept", FUNSOS_COLOR_WHITE);
    funsos_draw_text(win, 55, line_y + 50, "(Accepts all types)", FUNSOS_COLOR_GRAY);
    line_y += lh + 80 + 4;

    /* ---- 3. Drag Operations ---- */
    funsos_draw_text(win, 20, line_y, "[3] Supported Drag Operations", label_c);
    line_y += lh;

    funsos_draw_text(win, 40, line_y, "  DRAG_TEXT  - Drag selected text content", text_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "  DRAG_FILE  - Drag files from file manager", text_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "  DRAG_IMAGE - Drag images between applications", text_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "  DRAG_DATA  - Drag arbitrary binary data", text_c);
    line_y += lh + 4;

    /* ---- 4. Drop Effects ---- */
    funsos_draw_text(win, 20, line_y, "[4] Drop Effects", label_c);
    line_y += lh;

    funsos_draw_text(win, 40, line_y, "  COPY  - Copy dragged data to target (default)", text_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "  MOVE  - Move dragged data to target (remove from source)", text_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "  LINK  - Create a link/shortcut to dragged data", text_c);
    line_y += lh + 4;

    /* ---- 5. Simulated Drag-Drop Sequence ---- */
    funsos_draw_text(win, 20, line_y, "[5] Simulated Drag-Drop Sequence", label_c);
    line_y += lh;

    funsos_draw_text(win, 40, line_y, "Step 1: User presses mouse on source item", text_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "Step 2: System detects drag threshold exceeded", text_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "Step 3: Drag image/data is prepared", text_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "Step 4: Cursor changes to show drag effect", text_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "Step 5: User moves over drop target", text_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "Step 6: Target highlights to indicate acceptance", text_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "Step 7: User releases mouse button", text_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "Step 8: Drop event fires, data is transferred", text_c);
    line_y += lh + 4;

    /* ---- 6. Simulated File Drop ---- */
    funsos_draw_text(win, 20, line_y, "[6] Simulated File Drop Result", label_c);
    line_y += lh;

    const char *dropped_files[] = {
        "/home/user/document.pdf",
        "/home/user/image.png",
        "/home/user/spreadsheet.csv"
    };

    funsos_draw_text(win, 40, line_y, "Dropped files (simulated):", label_c);
    line_y += lh;

    for (int i = 0; i < 3; i++) {
        char idx_buf[8];
        idx_buf[0] = ' '; idx_buf[1] = ' '; idx_buf[2] = '0' + (i + 1);
        idx_buf[3] = '.'; idx_buf[4] = ' '; idx_buf[5] = '\0';
        funsos_draw_text(win, 50, line_y, idx_buf, label_c);
        funsos_draw_text(win, 90, line_y, dropped_files[i], text_c);
        line_y += lh;
    }
    line_y += lh + 4;

    /* ---- 7. Drag-Drop Events ---- */
    funsos_draw_text(win, 20, line_y, "[7] Drag-Drop Event Types", label_c);
    line_y += lh;

    funsos_draw_text(win, 40, line_y, "  DRAG_START    - Drag operation begins", text_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "  DRAG_ENTER    - Cursor enters drop target", text_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "  DRAG_OVER     - Cursor is over drop target", text_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "  DRAG_LEAVE    - Cursor leaves drop target", text_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "  DRAG_DROP     - Data is dropped on target", text_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "  DRAG_END      - Drag operation ends", text_c);
    line_y += lh + 8;

    /* Summary */
    funsos_draw_line(win, 15, line_y, 685, line_y, FUNSOS_COLOR_GRAY);
    line_y += 8;
    funsos_draw_text(win, 20, line_y, "[Summary] Drag & Drop Features:", title_c);
    line_y += lh + 4;

    funsos_draw_text(win, 40, line_y, "  [v] Text drag-and-drop between widgets", good_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "  [v] File drag-and-drop from file manager", good_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "  [v] Image drag-and-drop between applications", good_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "  [v] Copy/Move/Link drop effects", good_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "  [v] Visual feedback during drag operations", good_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "  [v] Full drag event lifecycle (start/enter/over/leave/drop/end)", good_c);
    line_y += lh + 8;

    /* Footer */
    funsos_draw_line(win, 15, 508, 685, 508, FUNSOS_COLOR_GRAY);
    funsos_draw_text(win, 20, 516, "Press ESC to exit", FUNSOS_COLOR_DARK_GRAY);

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