/* power_demo.c - Power Management Demo
 * Demonstrates battery status, power actions, brightness control,
 * and sleep inhibition using the funsos_power.h API.
 */

#include "funsos.h"

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
    funsos_window_t win = funsos_create_window(60, 40, 700, 500, "Power Management Demo");
    funsos_fill_window(win, FUNSOS_COLOR_WHITE);

    funsos_color_t title_c = FUNSOS_COLOR_BLUE;
    funsos_color_t text_c  = FUNSOS_COLOR_BLACK;
    funsos_color_t label_c = FUNSOS_COLOR_DARK_GRAY;
    funsos_color_t good_c  = FUNSOS_COLOR_GREEN;
    funsos_color_t warn_c  = FUNSOS_COLOR_ORANGE;
    funsos_color_t err_c   = FUNSOS_COLOR_RED;

    int line_y = 18;
    int lh = 22;
    char num_buf[32];

    /* Title */
    funsos_draw_text(win, 20, line_y, "=== Power Management Demo ===", title_c);
    line_y += lh + 4;
    funsos_draw_line(win, 15, line_y, 685, line_y, FUNSOS_COLOR_GRAY);
    line_y += 10;

    /* ---- 1. Power State ---- */
    funsos_draw_text(win, 20, line_y, "[1] Current Power State", label_c);
    line_y += lh;

    funsos_power_state_t state = funsos_power_get_state();
    const char *state_str = "Unknown";
    if (state == FUNSOS_POWER_STATE_AC) state_str = "AC Power (plugged in)";
    else if (state == FUNSOS_POWER_STATE_BATTERY) state_str = "Battery";
    else if (state == FUNSOS_POWER_STATE_CHARGING) state_str = "Charging";

    funsos_draw_text(win, 40, line_y, "Power Source: ", label_c);
    funsos_draw_text(win, 160, line_y, state_str, good_c);
    line_y += lh + 4;

    /* ---- 2. Battery Information ---- */
    funsos_draw_text(win, 20, line_y, "[2] Battery Information", label_c);
    line_y += lh;

    funsos_battery_info_t battery;
    int ret = funsos_battery_get_info(&battery);
    if (ret >= 0 && battery.present) {
        funsos_draw_text(win, 40, line_y, "Battery: Present", good_c);
        line_y += lh;

        /* Battery level */
        format_uint(battery.percent, num_buf, sizeof(num_buf));
        funsos_draw_text(win, 40, line_y, "  Level: ", label_c);
        funsos_draw_text(win, 100, line_y, num_buf, battery.critical ? err_c : good_c);
        funsos_draw_text(win, 120, line_y, "%", label_c);

        /* Draw battery bar */
        funsos_draw_rect(win, 160, line_y + 2, 100, 14, FUNSOS_COLOR_LIGHT_GRAY);
        int bar_w = (int)(100 * battery.percent / 100);
        if (bar_w > 0) {
            funsos_color_t bar_c = FUNSOS_COLOR_GREEN;
            if (battery.percent <= 20) bar_c = FUNSOS_COLOR_RED;
            else if (battery.percent <= 50) bar_c = FUNSOS_COLOR_ORANGE;
            funsos_draw_rect(win, 160, line_y + 2, bar_w, 14, bar_c);
        }
        line_y += lh;

        /* Charging status */
        funsos_draw_text(win, 40, line_y, "  Charging: ", label_c);
        funsos_draw_text(win, 120, line_y, battery.charging ? "Yes" : "No", battery.charging ? good_c : warn_c);
        line_y += lh;

        /* Remaining time */
        if (battery.remaining_minutes > 0) {
            format_uint(battery.remaining_minutes, num_buf, sizeof(num_buf));
            funsos_draw_text(win, 40, line_y, "  Remaining: ", label_c);
            funsos_draw_text(win, 130, line_y, num_buf, text_c);
            funsos_draw_text(win, 150, line_y, " minutes", label_c);
        } else {
            funsos_draw_text(win, 40, line_y, "  Remaining: Calculating...", label_c);
        }
        line_y += lh;

        /* Capacity */
        format_uint(battery.current_capacity_mwh, num_buf, sizeof(num_buf));
        funsos_draw_text(win, 40, line_y, "  Capacity: ", label_c);
        funsos_draw_text(win, 120, line_y, num_buf, text_c);
        format_uint(battery.full_capacity_mwh, num_buf, sizeof(num_buf));
        funsos_draw_text(win, 170, line_y, " / ", label_c);
        funsos_draw_text(win, 190, line_y, num_buf, text_c);
        funsos_draw_text(win, 220, line_y, " mWh", label_c);
        line_y += lh;

        /* Cycle count */
        format_uint(battery.cycle_count, num_buf, sizeof(num_buf));
        funsos_draw_text(win, 40, line_y, "  Cycles: ", label_c);
        funsos_draw_text(win, 110, line_y, num_buf, text_c);
        line_y += lh;

        /* Manufacturer */
        funsos_draw_text(win, 40, line_y, "  Manufacturer: ", label_c);
        funsos_draw_text(win, 150, line_y, battery.manufacturer, text_c);
        line_y += lh;

        if (battery.critical) {
            funsos_draw_text(win, 40, line_y, "  WARNING: Battery critical! Please connect charger.", err_c);
            line_y += lh;
        }
    } else {
        funsos_draw_text(win, 40, line_y, "No battery detected (desktop or VM)", warn_c);
    }
    line_y += lh + 4;

    /* ---- 3. Display Brightness ---- */
    funsos_draw_text(win, 20, line_y, "[3] Display Brightness Control", label_c);
    line_y += lh;

    int brightness = funsos_display_get_brightness();
    format_uint((uint32_t)brightness, num_buf, sizeof(num_buf));
    funsos_draw_text(win, 40, line_y, "Current Brightness: ", label_c);
    funsos_draw_text(win, 180, line_y, num_buf, good_c);
    funsos_draw_text(win, 200, line_y, "%", label_c);

    /* Draw brightness bar */
    funsos_draw_rect(win, 240, line_y + 2, 120, 14, FUNSOS_COLOR_LIGHT_GRAY);
    int bright_bar = (int)(120 * brightness / 100);
    if (bright_bar > 0) {
        funsos_draw_rect(win, 240, line_y + 2, bright_bar, 14, FUNSOS_COLOR_YELLOW);
    }
    line_y += lh;

    funsos_draw_text(win, 40, line_y, "Use funsos_display_set_brightness(N) to adjust (0-100)", label_c);
    line_y += lh + 4;

    /* ---- 4. CPU Information ---- */
    funsos_draw_text(win, 20, line_y, "[4] CPU Information", label_c);
    line_y += lh;

    funsos_cpu_info_t cpu;
    ret = funsos_cpu_get_info(&cpu);
    if (ret >= 0) {
        format_uint(cpu.current_freq_khz / 1000, num_buf, sizeof(num_buf));
        funsos_draw_text(win, 40, line_y, "Frequency: ", label_c);
        funsos_draw_text(win, 130, line_y, num_buf, text_c);
        funsos_draw_text(win, 150, line_y, " MHz", label_c);
        line_y += lh;

        format_uint(cpu.usage_percent, num_buf, sizeof(num_buf));
        funsos_draw_text(win, 40, line_y, "Usage: ", label_c);
        funsos_draw_text(win, 100, line_y, num_buf, text_c);
        funsos_draw_text(win, 120, line_y, "%", label_c);
        line_y += lh;

        format_uint(cpu.temperature_celsius, num_buf, sizeof(num_buf));
        funsos_draw_text(win, 40, line_y, "Temperature: ", label_c);
        funsos_draw_text(win, 140, line_y, num_buf, cpu.temperature_celsius > 80 ? err_c : good_c);
        funsos_draw_text(win, 160, line_y, " C", label_c);
    } else {
        funsos_draw_text(win, 40, line_y, "CPU info not available", warn_c);
    }
    line_y += lh + 4;

    /* ---- 5. Power Actions (simulated) ---- */
    funsos_draw_text(win, 20, line_y, "[5] Power Actions (simulated - not executed)", label_c);
    line_y += lh;

    funsos_draw_text(win, 40, line_y, "Available actions:", label_c);
    line_y += lh;
    funsos_draw_text(win, 50, line_y, "  [ ] power_suspend   - Suspend to RAM", text_c);
    line_y += lh;
    funsos_draw_text(win, 50, line_y, "  [ ] power_hibernate - Hibernate to disk", text_c);
    line_y += lh;
    funsos_draw_text(win, 50, line_y, "  [ ] power_reboot    - Reboot system", text_c);
    line_y += lh;
    funsos_draw_text(win, 50, line_y, "  [ ] power_shutdown  - Shutdown system", text_c);
    line_y += lh + 4;

    /* ---- 6. Sleep Inhibition ---- */
    funsos_draw_text(win, 20, line_y, "[6] Sleep Inhibition", label_c);
    line_y += lh;

    /* Simulate inhibiting sleep */
    ret = funsos_power_inhibit("Demo: power management showcase");
    if (ret >= 0) {
        funsos_draw_text(win, 40, line_y, "OK - Sleep inhibited (demo active)", good_c);
        line_y += lh;

        /* Simulate uninhibit */
        ret = funsos_power_uninhibit();
        if (ret >= 0) {
            funsos_draw_text(win, 40, line_y, "OK - Sleep uninhibited (demo complete)", good_c);
        } else {
            funsos_draw_text(win, 40, line_y, "WARNING: Could not uninhibit sleep", warn_c);
        }
    } else {
        funsos_draw_text(win, 40, line_y, "WARNING: Sleep inhibition not available", warn_c);
    }
    line_y += lh + 4;

    /* ---- 7. Idle Time ---- */
    funsos_draw_text(win, 20, line_y, "[7] System Idle Time", label_c);
    line_y += lh;

    uint32_t idle_time = funsos_idle_get_time();
    format_uint(idle_time, num_buf, sizeof(num_buf));
    funsos_draw_text(win, 40, line_y, "Idle: ", label_c);
    funsos_draw_text(win, 90, line_y, num_buf, text_c);
    funsos_draw_text(win, 110, line_y, " seconds", label_c);
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