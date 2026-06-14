/* spreadsheet.c - Spreadsheet application example
 * A simple spreadsheet with cell editing.
 */

#include "funsos.h"

#define SHEET_ROWS 10
#define SHEET_COLS 8
#define CELL_W 70
#define CELL_H 24
#define HEADER_H 24

static char cells[SHEET_ROWS][SHEET_COLS][32];
static int sel_row = 0, sel_col = 0;

int main(void)
{
    funsos_window_t win = funsos_create_window(60, 40, 700, 450, "Spreadsheet");
    funsos_fill_window(win, 0xFFFFFF);

    funsos_color_t black = {0x00, 0x00, 0x00, 0xFF};
    funsos_color_t blue  = {0x00, 0x00, 0xFF, 0xFF};
    funsos_color_t gray  = {0xE0, 0xE0, 0xE0, 0xFF};
    funsos_color_t sel_bg = {0xCC, 0xE8, 0xFF, 0xFF};

    /* Initialize cells */
    cells[0][0][0] = '1'; cells[0][0][1] = '\0';
    cells[0][1][0] = '2'; cells[0][1][1] = '\0';
    cells[1][0][0] = '3'; cells[1][0][1] = '\0';
    cells[1][1][0] = '4'; cells[1][1][1] = '\0';

    /* Event loop */
    funsos_event_t event;
    while (1) {
        if (funsos_wait_event(&event) != 0)
            continue;

        if (event.type == FUNSOS_EVENT_KEY_PRESS) {
            if (event.key == 0x1B) break;
            if (event.key == 0x09) { sel_col = (sel_col + 1) % SHEET_COLS; }  /* Tab */
            if (event.key == 0x0D) { sel_row = (sel_row + 1) % SHEET_ROWS; }  /* Enter */
            if (event.key == 0x26 && sel_row > 0) sel_row--;  /* Up */
            if (event.key == 0x28 && sel_row < SHEET_ROWS - 1) sel_row++;  /* Down */
            if (event.key == 0x25 && sel_col > 0) sel_col--;  /* Left */
            if (event.key == 0x27 && sel_col < SHEET_COLS - 1) sel_col++;  /* Right */

            if (event.key == 0x08) {  /* Backspace */
                int len = 0;
                while (cells[sel_row][sel_col][len]) len++;
                if (len > 0) cells[sel_row][sel_col][len - 1] = '\0';
            } else if (event.key >= 0x20 && event.key < 0x7F) {
                int len = 0;
                while (cells[sel_row][sel_col][len]) len++;
                if (len < 31) {
                    cells[sel_row][sel_col][len] = (char)event.key;
                    cells[sel_row][sel_col][len + 1] = '\0';
                }
            }
        }

        /* Render spreadsheet */
        funsos_fill_window(win, 0xFFFFFF);

        /* Column headers */
        for (int c = 0; c < SHEET_COLS; c++) {
            int x = c * CELL_W + HEADER_H;
            funsos_draw_rect(win, x, 0, CELL_W, HEADER_H, gray);
            char header[3] = {'A' + c, ':', '\0'};
            funsos_draw_text(win, x + 4, 6, header, black);
        }

        /* Row headers */
        for (int r = 0; r < SHEET_ROWS; r++) {
            int y = r * CELL_H + HEADER_H;
            funsos_draw_rect(win, 0, y, HEADER_H, CELL_H, gray);
            char header[4] = {'0' + r, '\0'};
            funsos_draw_text(win, 6, y + 6, header, black);
        }

        /* Cells */
        for (int r = 0; r < SHEET_ROWS; r++) {
            for (int c = 0; c < SHEET_COLS; c++) {
                int x = c * CELL_W + HEADER_H;
                int y = r * CELL_H + HEADER_H;

                if (r == sel_row && c == sel_col) {
                    funsos_draw_rect(win, x, y, CELL_W, CELL_H, sel_bg);
                }
                funsos_draw_rect(win, x, y, CELL_W, CELL_H, black);

                if (cells[r][c][0]) {
                    funsos_draw_text(win, x + 4, y + 6, cells[r][c], black);
                }
            }
        }

        /* Formula bar */
        funsos_draw_rect(win, 0, SHEET_ROWS * CELL_H + HEADER_H + 5,
                         700, 24, gray);
        funsos_draw_text(win, 4, SHEET_ROWS * CELL_H + HEADER_H + 10,
                         "Cell: ", black);
    }

    funsos_destroy_window(win);
    return 0;
}
