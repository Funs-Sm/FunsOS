/* theme_demo.c - Theme Switching and Customization Demo
 * Demonstrates how to switch between themes, customize colors,
 * and create custom theme configurations.
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
    funsos_window_t win = funsos_create_window(60, 40, 700, 520, "Theme Switching Demo");
    funsos_fill_window(win, FUNSOS_COLOR_WHITE);

    funsos_color_t title_c = FUNSOS_COLOR_BLUE;
    funsos_color_t text_c  = FUNSOS_COLOR_BLACK;
    funsos_color_t label_c = FUNSOS_COLOR_DARK_GRAY;
    funsos_color_t good_c  = FUNSOS_COLOR_GREEN;
    funsos_color_t warn_c  = FUNSOS_COLOR_ORANGE;
    funsos_color_t accent_c = {0x00, 0x78, 0xD4, 0xFF};

    int line_y = 18;
    int lh = 22;

    /* Title */
    funsos_draw_text(win, 20, line_y, "=== Theme System Demo ===", title_c);
    line_y += lh + 4;
    funsos_draw_line(win, 15, line_y, 685, line_y, FUNSOS_COLOR_GRAY);
    line_y += 10;

    /* ---- 1. List Available Themes ---- */
    funsos_draw_text(win, 20, line_y, "[1] Available Built-in Themes", label_c);
    line_y += lh;

    const char *themes[] = {
        "default", "dark", "light", "blue", "green", "high_contrast",
        "ocean", "forest", "sunset", "monochrome", "cyberpunk", "retro"
    };
    int num_themes = 12;

    for (int i = 0; i < num_themes; i++) {
        char idx_buf[8];
        idx_buf[0] = '0' + ((i + 1) / 10);
        idx_buf[1] = '0' + ((i + 1) % 10);
        idx_buf[2] = '.';
        idx_buf[3] = ' ';
        idx_buf[4] = '\0';

        funsos_draw_text(win, 40, line_y, idx_buf, label_c);
        funsos_draw_text(win, 70, line_y, themes[i], text_c);
        line_y += lh;

        if (i == 5) {
            /* Start a second column */
            line_y = 18 + lh + 4 + 10 + lh;
        }
    }

    line_y = 18 + lh + 4 + 10 + lh + 7 * lh + 4;

    /* ---- 2. Theme Switching Demo ---- */
    funsos_draw_text(win, 20, line_y, "[2] Theme Switching (simulated)", label_c);
    line_y += lh;

    funsos_draw_text(win, 40, line_y, "Current theme: ", label_c);
    funsos_draw_text(win, 160, line_y, "default", accent_c);
    line_y += lh;

    funsos_draw_text(win, 40, line_y, "Switch to 'dark' theme:  fr_set_theme(ctx, \"dark\")", label_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "Switch to 'light' theme: fr_set_theme(ctx, \"light\")", label_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "Switch to 'ocean' theme: fr_set_theme(ctx, \"ocean\")", label_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "Switch to 'cyberpunk':   fr_set_theme(ctx, \"cyberpunk\")", label_c);
    line_y += lh + 4;

    /* ---- 3. Theme Color Preview ---- */
    funsos_draw_text(win, 20, line_y, "[3] Theme Color Preview (default theme)", label_c);
    line_y += lh + 4;

    /* Color swatches */
    fr_color_t swatches[] = {
        FR_RGB(255, 255, 255),  /* window_bg */
        FR_RGB(0, 0, 0),        /* window_fg */
        FR_RGB(0, 120, 212),    /* accent */
        FR_RGB(230, 230, 230),  /* button_bg */
        FR_RGB(224, 0, 0),      /* error */
        FR_RGB(255, 170, 0),    /* warning */
        FR_RGB(0, 128, 0),      /* success */
        FR_RGB(0, 120, 212),    /* title bar */
    };
    const char *swatch_names[] = {
        " WinBG", " WinFG", "Accent", "BtnBG", " Error", "  Warn", "  Good", "Title"
    };

    int sx = 40;
    for (int i = 0; i < 8; i++) {
        funsos_draw_rect(win, sx, line_y, 24, 18, FUNSOS_COLOR_BLACK);
        funsos_draw_rect(win, sx + 1, line_y + 1, 22, 16, swatches[i]);
        funsos_draw_text(win, sx + 30, line_y, swatch_names[i], label_c);
        sx += 80;
    }
    line_y += lh + 8;

    /* ---- 4. Font Settings ---- */
    funsos_draw_text(win, 20, line_y, "[4] Font Customization", label_c);
    line_y += lh;

    funsos_draw_text(win, 40, line_y, "Default font: sans (14px)", text_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "Title font:   sans (16px)", text_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "Mono font:    mono (11px)", text_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "Set font: fr_set_font(ctx, \"Tahoma\", 12)", label_c);
    line_y += lh + 4;

    /* ---- 5. Metrics Settings ---- */
    funsos_draw_text(win, 20, line_y, "[5] Theme Metrics Customization", label_c);
    line_y += lh;

    funsos_draw_text(win, 40, line_y, "Border radius:     4px", text_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "Border width:      1px", text_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "Shadow radius:     8px (offset 2,2)", text_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "Padding:           8px", text_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "Spacing:           4px", text_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "Title bar height:  28px", text_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "Scrollbar width:   16px", text_c);
    line_y += lh + 4;

    /* ---- 6. Theme Transition ---- */
    funsos_draw_text(win, 20, line_y, "[6] Theme Transition Animation", label_c);
    line_y += lh;

    funsos_draw_text(win, 40, line_y, "Smooth transition between themes (300ms default)", text_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "  fr_theme_transition_start(ctx, \"dark\", 500)", label_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "  fr_theme_transition_update(get_ticks())", label_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "All colors linearly interpolated during transition", text_c);
    line_y += lh + 4;

    /* ---- 7. Custom Theme Creation ---- */
    funsos_draw_text(win, 20, line_y, "[7] Creating a Custom Theme", label_c);
    line_y += lh;

    funsos_draw_text(win, 40, line_y, "1. Define a fr_theme_t structure with custom colors", text_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "2. Call fr_theme_register(&my_theme) to register", text_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "3. Call fr_theme_set_active(ctx, \"my_theme\") to activate", text_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "4. Use widget color overrides for fine-grained control", text_c);
    line_y += lh + 8;

    /* Summary */
    funsos_draw_line(win, 15, line_y, 685, line_y, FUNSOS_COLOR_GRAY);
    line_y += 8;
    funsos_draw_text(win, 20, line_y, "[Summary] Theme features:", title_c);
    line_y += lh + 4;
    funsos_draw_text(win, 40, line_y, "  [v] 12 built-in themes (default, dark, light, ocean, forest, etc.)", good_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "  [v] Color, font, and metric customization", good_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "  [v] Smooth transition animations", good_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "  [v] Widget-level color overrides", good_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "  [v] Theme export/import to file", good_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "  [v] Theme blending (mix two themes)", good_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "  [v] Auto dark/light detection by time", good_c);
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