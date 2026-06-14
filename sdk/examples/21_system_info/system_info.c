/* system_info.c - System Information Query Example
 * Demonstrates querying CPU info, memory stats, uptime,
 * system version, and other kernel-level information
 * using the funsos_sysinfo.h API.
 */

#include "funsos.h"

/* Helper: format a number with comma separators for display */
static void format_number(uint32_t value, char *buf, int bufsize)
{
    char tmp[32];
    int i = 0, j = 0;

    if (value == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }

    while (value > 0 && i < (int)sizeof(tmp) - 1) {
        tmp[i++] = '0' + (value % 10);
        value /= 10;
    }
    tmp[i] = '\0';

    /* Reverse and insert commas every 3 digits */
    int pos = 0;
    int digit_count = 0;
    for (int k = i - 1; k >= 0 && pos < bufsize - 2; k--) {
        if (digit_count > 0 && digit_count % 3 == 0) {
            buf[pos++] = ',';
        }
        buf[pos++] = tmp[k];
        digit_count++;
    }
    buf[pos] = '\0';
}

/* Helper: convert seconds to a human-readable uptime string */
static void format_uptime(uint32_t seconds, char *buf, int bufsize)
{
    uint32_t days = seconds / 86400;
    uint32_t hours = (seconds % 86400) / 3600;
    uint32_t mins = (seconds % 3600) / 60;
    uint32_t secs = seconds % 60;

    if (days > 0) {
        /* snprintf-like formatting manually */
        int pos = 0;
        /* Days */
        if (days < 10) buf[pos++] = '0' + days; else { buf[pos++] = '0' + (days/10); buf[pos++] = '0' + (days%10); }
        buf[pos++] = 'd'; buf[pos++] = ' ';
        /* Hours */
        if (hours < 10) buf[pos++] = '0' + hours; else { buf[pos++] = '0' + (hours/10); buf[pos++] = '0' + (hours%10); }
        buf[pos++] = ':'; /* Minutes */
        if (mins < 10) buf[pos++] = '0' + mins; else { buf[pos++] = '0' + (mins/10); buf[pos++] = '0' + (mins%10); }
        buf[pos++] = ':'; /* Seconds */
        if (secs < 10) buf[pos++] = '0' + secs; else { buf[pos++] = '0' + (secs/10); buf[pos++] = '0' + (secs%10); }
        buf[pos] = '\0';
    } else {
        int pos = 0;
        if (hours < 10) buf[pos++] = '0' + hours; else { buf[pos++] = '0' + (hours/10); buf[pos++] = '0' + (hours%10); }
        buf[pos++] = ':';
        if (mins < 10) buf[pos++] = '0' + mins; else { buf[pos++] = '0' + (mins/10); buf[pos++] = '0' + (mins%10); }
        buf[pos++] = ':';
        if (secs < 10) buf[pos++] = '0' + secs; else { buf[pos++] = '0' + (secs/10); buf[pos++] = '0' + (secs%10); }
        buf[pos] = '\0';
    }
}

int main(void)
{
    /* Create main window for displaying system information */
    funsos_window_t win = funsos_create_window(80, 40, 640, 520, "System Information");
    funsos_fill_window(win, FUNSOS_COLOR_WHITE);

    /* Colors used throughout the display */
    funsos_color_t title_color   = FUNSOS_COLOR_BLUE;
    funsos_color_t label_color   = FUNSOS_COLOR_DARK_GRAY;
    funsos_color_t value_color   = FUNSOS_COLOR_BLACK;
    funsos_color_t highlight     = FUNSOS_COLOR_RED;
    funsos_color_t accent        = FUNSOS_COLOR_GREEN;

    /* ---- Section 1: Title bar ---- */
    funsos_draw_text(win, 20, 15, "=== FUNSOS System Information ===", title_color);

    /* Draw separator line under title */
    funsos_draw_line(win, 15, 35, 625, 35, FUNSOS_COLOR_GRAY);

    /* ---- Query system information from kernel ---- */
    funsos_sysinfo_t sysinfo;
    int ret = funsos_get_sysinfo(&sysinfo);
    if (ret != 0) {
        funsos_draw_text(win, 20, 55, "ERROR: Failed to query system information!", highlight);
    } else {
        int y = 55;
        int line_height = 24;
        char num_buf[32];
        char time_buf[32];

        /* --- Operating System & Kernel Info --- */
        funsos_draw_rect(win, 15, y - 4, 610, line_height + 4, FUNSOS_COLOR_LIGHT_GRAY);
        funsos_draw_text(win, 20, y, "[Operating System]", label_color);
        y += line_height + 8;

        funsos_draw_text(win, 30, y, "OS Name:      ", label_color);
        funsos_draw_text(win, 140, y, sysinfo.os_name, value_color);
        y += line_height;

        funsos_draw_text(win, 30, y, "Kernel Name:  ", label_color);
        funsos_draw_text(win, 140, y, sysinfo.kernel_name, value_color);
        y += line_height;

        funsos_draw_text(win, 30, y, "Kernel Ver:   ", label_color);
        funsos_draw_text(win, 140, y, sysinfo.kernel_version, value_color);
        y += line_height;

        funsos_draw_text(win, 30, y, "SDK Version:  ", label_color);
        funsos_draw_text(win, 140, y, FUNSOS_SDK_VERSION, accent);
        y += line_height + 12;

        /* --- CPU Information --- */
        funsos_draw_rect(win, 15, y - 4, 610, line_height + 4, FUNSOS_COLOR_LIGHT_GRAY);
        funsos_draw_text(win, 20, y, "[CPU Information]", label_color);
        y += line_height + 8;

        format_number(sysinfo.cpu_count, num_buf, sizeof(num_buf));
        funsos_draw_text(win, 30, y, "CPU Cores:    ", label_color);
        funsos_draw_text(win, 140, y, num_buf, value_color);
        y += line_height;

        funsos_draw_text(win, 30, y, "Architecture: ", label_color);
        funsos_draw_text(win, 140, y, "x86 (32-bit Protected Mode)", value_color);
        y += line_height + 12;

        /* --- Memory Statistics --- */
        funsos_draw_rect(win, 15, y - 4, 610, line_height + 4, FUNSOS_COLOR_LIGHT_GRAY);
        funsos_draw_text(win, 20, y, "[Memory Statistics]", label_color);
        y += line_height + 8;

        format_number(sysinfo.total_memory, num_buf, sizeof(num_buf));
        funsos_draw_text(win, 30, y, "Total Memory: ", label_color);
        funsos_draw_text(win, 140, y, num_buf, value_color);
        funsos_draw_text(win, 280, y, " KB", label_color);
        y += line_height;

        format_number(sysinfo.used_memory, num_buf, sizeof(num_buf));
        funsos_draw_text(win, 30, y, "Used Memory:  ", label_color);
        funsos_draw_text(win, 140, y, num_buf, highlight);
        funsos_draw_text(win, 280, y, " KB", label_color);
        y += line_height;

        /* Calculate and display memory usage percentage */
        if (sysinfo.total_memory > 0) {
            uint32_t percent = (sysinfo.used_memory * 100) / sysinfo.total_memory;
            char pct_buf[16];
            int p = 0;
            if (percent >= 100) { pct_buf[p++] = '0' + (percent/100); percent %= 100; }
            if (percent >= 10 || p > 0) { pct_buf[p++] = '0' + (percent/10); percent %= 10; }
            pct_buf[p++] = '0' + percent;
            pct_buf[p++] = '%'; pct_buf[p++] = '\0';

            funsos_draw_text(win, 30, y, "Usage:        ", label_color);
            funsos_draw_text(win, 140, y, pct_buf,
                             (sysinfo.used_memory * 100 / sysinfo.total_memory > 80) ? highlight : accent);
            /* Draw simple usage bar */
            funsos_draw_rect(win, 300, y + 2, 200, 14, FUNSOS_COLOR_LIGHT_GRAY);
            int bar_width = (int)(200 * sysinfo.used_memory / sysinfo.total_memory);
            if (bar_width > 0) {
                funsos_color_t bar_color = (sysinfo.used_memory * 100 / sysinfo.total_memory > 80)
                                           ? FUNSOS_COLOR_RED : FUNSOS_COLOR_GREEN;
                funsos_draw_rect(win, 300, y + 2, bar_width, 14, bar_color);
            }
        }
        y += line_height + 12;

        /* --- System Uptime --- */
        format_uptime(sysinfo.uptime, time_buf, sizeof(time_buf));
        funsos_draw_rect(win, 15, y - 4, 610, line_height + 4, FUNSOS_COLOR_LIGHT_GRAY);
        funsos_draw_text(win, 20, y, "[System Status]", label_color);
        y += line_height + 8;

        funsos_draw_text(win, 30, y, "Uptime:       ", label_color);
        funsos_draw_text(win, 140, y, time_buf, value_color);
        y += line_height;

        format_number(sysinfo.process_count, num_buf, sizeof(num_buf));
        funsos_draw_text(win, 30, y, "Processes:    ", label_color);
        funsos_draw_text(win, 140, y, num_buf, value_color);
        y += line_height;

        /* Get current timestamp */
        uint32_t cur_time = funsos_get_time();
        funsos_draw_text(win, 30, y, "Unix Time:    ", label_color);
        format_number(cur_time, num_buf, sizeof(num_buf));
        funsos_draw_text(win, 140, y, num_buf, value_color);
        y += line_height + 12;

        /* --- Additional Info via ticks --- */
        uint32_t ticks = funsos_get_ticks();
        funsos_draw_text(win, 30, y, "System Ticks: ", label_color);
        format_number(ticks, num_buf, sizeof(num_buf));
        funsos_draw_text(win, 140, y, num_buf, value_color);
    }

    /* Footer hint */
    funsos_draw_line(win, 15, 500, 625, 500, FUNSOS_COLOR_GRAY);
    funsos_draw_text(win, 20, 508, "Press ESC to exit", FUNSOS_COLOR_DARK_GRAY);

    /* Event loop - wait for user to close the window */
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
