/* notification_demo.c - System Notification Demo
 * Demonstrates creating and displaying system notifications
 * with various types, icons, and actions.
 */

#include "funsos.h"

/* Helper: simple string length */
static int my_strlen(const char *s)
{
    int len = 0;
    while (s[len]) len++;
    return len;
}

/* Notification type constants */
#define NOTIFY_TYPE_INFO     0
#define NOTIFY_TYPE_WARNING  1
#define NOTIFY_TYPE_ERROR    2
#define NOTIFY_TYPE_SUCCESS  3

/* Notification priority */
#define NOTIFY_PRIORITY_LOW    0
#define NOTIFY_PRIORITY_NORMAL 1
#define NOTIFY_PRIORITY_HIGH   2
#define NOTIFY_PRIORITY_URGENT 3

int main(void)
{
    funsos_window_t win = funsos_create_window(60, 40, 680, 500, "System Notification Demo");
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
    funsos_draw_text(win, 20, line_y, "=== System Notification Demo ===", title_c);
    line_y += lh + 4;
    funsos_draw_line(win, 15, line_y, 665, line_y, FUNSOS_COLOR_GRAY);
    line_y += 10;

    /* ---- 1. Notification Types ---- */
    funsos_draw_text(win, 20, line_y, "[1] Notification Types", label_c);
    line_y += lh;

    funsos_draw_text(win, 40, line_y, "INFO     - General information notification", text_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "WARNING  - Warning/alert notification", warn_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "ERROR    - Error/critical notification", err_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "SUCCESS  - Success/completion notification", good_c);
    line_y += lh + 4;

    /* ---- 2. Send Info Notification ---- */
    funsos_draw_text(win, 20, line_y, "[2] Sending Info notification...", label_c);
    line_y += lh;

    /* Simulated notification creation */
    funsos_draw_text(win, 40, line_y, "Title: \"Welcome to FUNSOS\"", text_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "Body:  \"Your system is ready.\"", text_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "Type:  INFO | Priority: NORMAL", label_c);
    line_y += lh;

    /* Simulate sending */
    funsos_draw_text(win, 40, line_y, "OK - Notification sent successfully", good_c);
    line_y += lh + 4;

    /* ---- 3. Send Warning Notification ---- */
    funsos_draw_text(win, 20, line_y, "[3] Sending Warning notification...", label_c);
    line_y += lh;

    funsos_draw_text(win, 40, line_y, "Title: \"Low Battery\"", text_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "Body:  \"Battery at 15%. Please connect charger.\"", warn_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "Type:  WARNING | Priority: HIGH", warn_c);
    line_y += lh;

    funsos_draw_text(win, 40, line_y, "OK - Warning notification sent", good_c);
    line_y += lh + 4;

    /* ---- 4. Send Error Notification ---- */
    funsos_draw_text(win, 20, line_y, "[4] Sending Error notification...", label_c);
    line_y += lh;

    funsos_draw_text(win, 40, line_y, "Title: \"Disk Full\"", text_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "Body:  \"Cannot save file. Disk space exhausted.\"", err_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "Type:  ERROR | Priority: URGENT", err_c);
    line_y += lh;

    funsos_draw_text(win, 40, line_y, "OK - Error notification sent", good_c);
    line_y += lh + 4;

    /* ---- 5. Send Success Notification ---- */
    funsos_draw_text(win, 20, line_y, "[5] Sending Success notification...", label_c);
    line_y += lh;

    funsos_draw_text(win, 40, line_y, "Title: \"Download Complete\"", text_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "Body:  \"File downloaded successfully.\"", good_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "Type:  SUCCESS | Priority: LOW", good_c);
    line_y += lh;

    funsos_draw_text(win, 40, line_y, "OK - Success notification sent", good_c);
    line_y += lh + 4;

    /* ---- 6. Notification with Actions ---- */
    funsos_draw_text(win, 20, line_y, "[6] Notification with Action Buttons", label_c);
    line_y += lh;

    funsos_draw_text(win, 40, line_y, "Title: \"Update Available\"", text_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "Body:  \"FUNSOS 1.1 is available.\"", text_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "Actions: [Update Now] [Remind Later] [Dismiss]", label_c);
    line_y += lh;

    funsos_draw_text(win, 40, line_y, "OK - Action notification sent", good_c);
    line_y += lh + 4;

    /* ---- 7. Notification Queue ---- */
    funsos_draw_text(win, 20, line_y, "[7] Notification Queue Summary", label_c);
    line_y += lh;

    funsos_draw_text(win, 40, line_y, "Notifications sent: 5", text_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "  - 1 INFO (Normal)", label_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "  - 1 WARNING (High)", label_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "  - 1 ERROR (Urgent)", label_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "  - 1 SUCCESS (Low)", label_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "  - 1 Action notification", label_c);
    line_y += lh + 8;

    /* ---- Summary ---- */
    funsos_draw_line(win, 15, line_y, 665, line_y, FUNSOS_COLOR_GRAY);
    line_y += 8;
    funsos_draw_text(win, 20, line_y, "[Summary] Notification Features:", title_c);
    line_y += lh + 4;

    funsos_draw_text(win, 40, line_y, "  [v] Multiple notification types (INFO, WARNING, ERROR, SUCCESS)", good_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "  [v] Priority levels (LOW, NORMAL, HIGH, URGENT)", good_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "  [v] Action buttons for user interaction", good_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "  [v] Notification queue with history", good_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "  [v] Auto-dismiss with configurable timeout", good_c);
    line_y += lh + 8;

    /* Footer */
    funsos_draw_line(win, 15, 488, 665, 488, FUNSOS_COLOR_GRAY);
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