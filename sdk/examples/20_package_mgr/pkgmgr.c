/* pkgmgr.c - Package manager example
 * Demonstrates a package management system for FUNSOS.
 */

#include "funsos.h"

typedef struct {
    char name[32];
    char version[16];
    char desc[64];
    int installed;
} package_t;

static package_t packages[] = {
    {"editor",    "1.2.0", "Text editor",          1},
    {"browser",   "0.8.0", "Web browser",          0},
    {"terminal",  "2.0.1", "Terminal emulator",     1},
    {"calc",      "1.0.0", "Calculator",           1},
    {"paint",     "0.5.0", "Drawing application",   0},
    {"music",     "1.1.0", "Music player",          0},
    {"mail",      "0.3.0", "Email client",          0},
    {"calendar",  "1.0.0", "Calendar app",          0},
    {"notes",     "0.9.0", "Note-taking app",       1},
    {"weather",   "0.2.0", "Weather widget",        0},
};
#define PKG_COUNT 10

int main(void)
{
    funsos_window_t win = funsos_create_window(80, 50, 600, 450, "Package Manager");
    funsos_fill_window(win, 0xFFFFFF);

    funsos_color_t black = {0x00, 0x00, 0x00, 0xFF};
    funsos_color_t blue  = {0x00, 0x78, 0xD4, 0xFF};
    funsos_color_t green = {0x00, 0x80, 0x00, 0xFF};
    funsos_color_t gray  = {0xF0, 0xF0, 0xF0, 0xFF};
    funsos_color_t white = {0xFF, 0xFF, 0xFF, 0xFF};

    int selected = 0;

    /* Event loop */
    funsos_event_t event;
    while (1) {
        if (funsos_wait_event(&event) != 0)
            continue;

        if (event.type == FUNSOS_EVENT_KEY_PRESS) {
            if (event.key == 0x1B) break;
            if (event.key == 0x26 && selected > 0) selected--;  /* Up */
            if (event.key == 0x28 && selected < PKG_COUNT - 1) selected++;  /* Down */
            if (event.key == 'i') {  /* Install */
                packages[selected].installed = 1;
            }
            if (event.key == 'u') {  /* Uninstall */
                packages[selected].installed = 0;
            }
        }

        /* Render */
        funsos_fill_window(win, 0xFFFFFF);

        /* Header */
        funsos_draw_rect(win, 0, 0, 600, 40, blue);
        funsos_draw_text(win, 20, 12, "FUNSOS Package Manager", white);

        /* Package list */
        for (int i = 0; i < PKG_COUNT; i++) {
            int y = 50 + i * 36;

            if (i == selected) {
                funsos_draw_rect(win, 10, y, 580, 32, gray);
            }

            /* Name */
            funsos_draw_text(win, 20, y + 8, packages[i].name, black);

            /* Version */
            funsos_draw_text(win, 150, y + 8, packages[i].version, black);

            /* Description */
            funsos_draw_text(win, 230, y + 8, packages[i].desc, black);

            /* Status */
            if (packages[i].installed) {
                funsos_draw_text(win, 510, y + 8, "[Installed]", green);
            } else {
                funsos_draw_text(win, 510, y + 8, "[Available]", blue);
            }
        }

        /* Footer */
        funsos_draw_rect(win, 0, 420, 600, 30, gray);
        funsos_draw_text(win, 20, 426, "I=Install  U=Uninstall  Up/Down=Select  ESC=Quit", black);
    }

    funsos_destroy_window(win);
    return 0;
}
