/* fuse_demo.c - FUSE filesystem example
 * Demonstrates creating a custom filesystem using FUSE.
 */

#include "funsos.h"

int main(void)
{
    funsos_window_t win = funsos_create_window(80, 60, 550, 380, "FUSE Demo");
    funsos_fill_window(win, 0xFFFFFF);

    funsos_color_t black = {0x00, 0x00, 0x00, 0xFF};
    funsos_color_t blue  = {0x00, 0x00, 0xFF, 0xFF};
    funsos_color_t green = {0x00, 0x80, 0x00, 0xFF};

    funsos_draw_text(win, 20, 20, "FUSE Filesystem Demo", blue);

    /* FUSE operations structure */
    funsos_draw_text(win, 20, 60, "FUSE allows creating custom filesystems", black);
    funsos_draw_text(win, 20, 85, "in user space without kernel modules.", black);

    funsos_draw_text(win, 20, 120, "Example FUSE operations:", black);
    funsos_draw_text(win, 40, 145, "getattr  - Get file attributes", black);
    funsos_draw_text(win, 40, 170, "readdir  - Read directory entries", black);
    funsos_draw_text(win, 40, 195, "read     - Read file data", black);
    funsos_draw_text(win, 40, 220, "write    - Write file data", black);
    funsos_draw_text(win, 40, 245, "mkdir    - Create directory", black);
    funsos_draw_text(win, 40, 270, "unlink   - Remove file", black);

    funsos_draw_text(win, 20, 310, "Mount a FUSE filesystem:", green);
    funsos_draw_text(win, 40, 335, "funsos_fuse_main(&ops, \"/mnt/myfs\")", blue);

    /* Event loop */
    funsos_event_t event;
    while (1) {
        if (funsos_wait_event(&event) != 0)
            continue;
        if (event.type == FUNSOS_EVENT_KEY_PRESS && event.key == 0x1B)
            break;
    }

    funsos_destroy_window(win);
    return 0;
}
