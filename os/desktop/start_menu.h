#ifndef START_MENU_H
#define START_MENU_H
#include "stdint.h"

#define START_MENU_WIDTH    240
#define START_MENU_HEIGHT   360
#define START_MENU_ITEM_H   32

typedef enum {
    MENU_ITEM_APP = 0,
    MENU_ITEM_SEPARATOR,
    MENU_ITEM_ACTION,
    MENU_ITEM_SUBMENU
} start_menu_item_type_t;

typedef struct {
    char label[64];
    char icon_path[128];
    char exec_path[128];
    start_menu_item_type_t type;
    int x, y, w, h;
} start_menu_item_t;

void start_menu_init(int screen_w, int screen_h, uint32_t *fb, uint32_t pitch);
void start_menu_cleanup(void);
void start_menu_show(void);
void start_menu_hide(void);
void start_menu_toggle(void);
int start_menu_is_visible(void);
void start_menu_render(void);
int start_menu_handle_click(int x, int y);
int start_menu_handle_hover(int x, int y);
int start_menu_add_item(const start_menu_item_t *item);
int start_menu_remove_item(const char *label);

#endif