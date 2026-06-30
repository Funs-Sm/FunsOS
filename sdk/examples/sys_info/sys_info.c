/* sys_info.c - 系统信息查询示例程序
 * 演示如何查询 FUNSOS 系统的各种信息，包括 OS 版本、CPU、内存、进程等。
 *
 * 功能说明：
 *   - 获取操作系统版本信息
 *   - 查询 CPU 信息和核心数
 *   - 查询内存使用情况
 *   - 获取系统运行时间 (uptime)
 *   - 查询进程数量
 *   - 获取系统时间和时钟滴答数
 *
 * 使用的主要 API：
 *   - funsos_get_sysinfo() - 获取完整系统信息
 *   - funsos_get_version() - 获取内核版本
 *   - funsos_get_memory_info() - 获取内存信息
 *   - funsos_get_time() - 获取当前时间
 *   - funsos_get_ticks() - 获取系统滴答数
 */

#include "funsos.h"

/* 辅助函数：格式化数字（添加千位分隔符）*/
static void format_number(uint32_t value, char *buf, int bufsize)
{
    char tmp[32];
    int i = 0;

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

/* 辅助函数：格式化运行时间 */
static void format_uptime(uint32_t seconds, char *buf, int bufsize)
{
    uint32_t days = seconds / 86400;
    uint32_t hours = (seconds % 86400) / 3600;
    uint32_t mins = (seconds % 3600) / 60;
    uint32_t secs = seconds % 60;

    int pos = 0;

    if (days > 0) {
        if (days < 10) buf[pos++] = '0' + days;
        else { buf[pos++] = '0' + (days / 10); buf[pos++] = '0' + (days % 10); }
        buf[pos++] = 'd'; buf[pos++] = ' ';
    }

    if (hours < 10) buf[pos++] = '0' + hours;
    else { buf[pos++] = '0' + (hours / 10); buf[pos++] = '0' + (hours % 10); }
    buf[pos++] = ':';

    if (mins < 10) buf[pos++] = '0' + mins;
    else { buf[pos++] = '0' + (mins / 10); buf[pos++] = '0' + (mins % 10); }
    buf[pos++] = ':';

    if (secs < 10) buf[pos++] = '0' + secs;
    else { buf[pos++] = '0' + (secs / 10); buf[pos++] = '0' + (secs % 10); }
    buf[pos] = '\0';
}

/* 在窗口中绘制带标签的值 */
static void draw_info_line(funsos_window_t win, int y,
                           const char *label, const char *value,
                           funsos_color_t value_color)
{
    funsos_draw_text(win, 30, y, label, FUNSOS_COLOR_DARK_GRAY);
    funsos_draw_text(win, 180, y, value, value_color);
}

/* 绘制一个分段 */
static void draw_section(funsos_window_t win, int y, const char *title)
{
    funsos_fill_rect(win, 15, y - 4, 570, 22, FUNSOS_COLOR_LIGHT_GRAY);
    funsos_draw_text(win, 20, y, title, FUNSOS_COLOR_BLUE);
}

int main(void)
{
    int line_y = 15;
    char num_buf[32];
    char time_buf[32];

    /* 创建窗口 */
    funsos_window_t win = funsos_create_window(80, 40, 600, 520, "System Information");
    funsos_fill_window(win, FUNSOS_COLOR_WHITE);

    /* 标题 */
    funsos_draw_text(win, 20, line_y, "=== FUNSOS System Information ===", FUNSOS_COLOR_BLUE);
    line_y += 25;
    funsos_draw_line(win, 15, line_y, 585, line_y, FUNSOS_COLOR_GRAY);
    line_y += 15;

    /* 获取系统信息 */
    funsos_sysinfo_t sysinfo;
    int ret = funsos_get_sysinfo(&sysinfo);

    if (ret != 0) {
        funsos_draw_text(win, 20, line_y, "ERROR: Failed to get system info!", FUNSOS_COLOR_RED);
    } else {
        /* --- 操作系统信息 --- */
        draw_section(win, line_y, "[ Operating System ]");
        line_y += 25;

        draw_info_line(win, line_y, "OS Name:", sysinfo.os_name, FUNSOS_COLOR_BLACK);
        line_y += 22;

        draw_info_line(win, line_y, "Kernel Name:", sysinfo.kernel_name, FUNSOS_COLOR_BLACK);
        line_y += 22;

        draw_info_line(win, line_y, "Kernel Version:", sysinfo.kernel_version, FUNSOS_COLOR_GREEN);
        line_y += 22;

        draw_info_line(win, line_y, "SDK Version:", FUNSOS_SDK_VERSION, FUNSOS_COLOR_RED);
        line_y += 30;

        /* --- CPU 信息 --- */
        draw_section(win, line_y, "[ CPU Information ]");
        line_y += 25;

        format_number(sysinfo.cpu_count, num_buf, sizeof(num_buf));
        draw_info_line(win, line_y, "CPU Cores:", num_buf, FUNSOS_COLOR_BLACK);
        line_y += 22;

        draw_info_line(win, line_y, "Architecture:", "x86 (32-bit)", FUNSOS_COLOR_BLACK);
        line_y += 22;

        draw_info_line(win, line_y, "Mode:", "Protected Mode", FUNSOS_COLOR_BLACK);
        line_y += 30;

        /* --- 内存信息 --- */
        draw_section(win, line_y, "[ Memory Statistics ]");
        line_y += 25;

        format_number(sysinfo.total_memory, num_buf, sizeof(num_buf));
        strcat(num_buf, " KB");
        draw_info_line(win, line_y, "Total Memory:", num_buf, FUNSOS_COLOR_BLACK);
        line_y += 22;

        format_number(sysinfo.used_memory, num_buf, sizeof(num_buf));
        strcat(num_buf, " KB");
        draw_info_line(win, line_y, "Used Memory:", num_buf, FUNSOS_COLOR_RED);
        line_y += 22;

        /* 计算可用内存 */
        uint32_t free_mem = sysinfo.total_memory - sysinfo.used_memory;
        format_number(free_mem, num_buf, sizeof(num_buf));
        strcat(num_buf, " KB");
        draw_info_line(win, line_y, "Free Memory:", num_buf, FUNSOS_COLOR_GREEN);
        line_y += 22;

        /* 内存使用百分比 */
        if (sysinfo.total_memory > 0) {
            uint32_t percent = (sysinfo.used_memory * 100) / sysinfo.total_memory;
            char pct_buf[8];
            int p = 0;
            if (percent >= 100) { pct_buf[p++] = '0' + (percent / 100); percent %= 100; }
            if (percent >= 10 || p > 0) { pct_buf[p++] = '0' + (percent / 10); percent %= 10; }
            pct_buf[p++] = '0' + percent;
            pct_buf[p++] = '%'; pct_buf[p] = '\0';

            funsos_color_t pct_color = (sysinfo.used_memory * 100 / sysinfo.total_memory > 80)
                                       ? FUNSOS_COLOR_RED : FUNSOS_COLOR_GREEN;
            draw_info_line(win, line_y, "Usage:", pct_buf, pct_color);

            /* 绘制进度条 */
            funsos_draw_rect(win, 180, line_y + 2, 200, 12, FUNSOS_COLOR_GRAY);
            int bar_width = (int)(200 * sysinfo.used_memory / sysinfo.total_memory);
            if (bar_width > 0) {
                funsos_fill_rect(win, 180, line_y + 2, bar_width, 12, pct_color);
            }
        }
        line_y += 30;

        /* --- 系统状态 --- */
        draw_section(win, line_y, "[ System Status ]");
        line_y += 25;

        format_uptime(sysinfo.uptime, time_buf, sizeof(time_buf));
        draw_info_line(win, line_y, "Uptime:", time_buf, FUNSOS_COLOR_BLACK);
        line_y += 22;

        format_number(sysinfo.process_count, num_buf, sizeof(num_buf));
        draw_info_line(win, line_y, "Processes:", num_buf, FUNSOS_COLOR_BLACK);
        line_y += 22;

        /* 当前时间 */
        uint32_t cur_time = funsos_get_time();
        format_number(cur_time, num_buf, sizeof(num_buf));
        draw_info_line(win, line_y, "Unix Time:", num_buf, FUNSOS_COLOR_BLACK);
        line_y += 22;

        /* 系统滴答数 */
        uint32_t ticks = funsos_get_ticks();
        format_number(ticks, num_buf, sizeof(num_buf));
        draw_info_line(win, line_y, "System Ticks:", num_buf, FUNSOS_COLOR_BLACK);
        line_y += 30;

        /* --- 内核版本 --- */
        draw_section(win, line_y, "[ Kernel Info ]");
        line_y += 25;

        const char *version = funsos_get_version();
        draw_info_line(win, line_y, "Version String:", version, FUNSOS_COLOR_GREEN);
        line_y += 22;

        /* 内存信息查询 */
        uint32_t total, used;
        funsos_get_memory_info(&total, &used);
        format_number(total, num_buf, sizeof(num_buf));
        draw_info_line(win, line_y, "Mem Info (API):", num_buf, FUNSOS_COLOR_DARK_GRAY);
    }

    /* 底部提示 */
    funsos_draw_line(win, 15, 500, 585, 500, FUNSOS_COLOR_GRAY);
    funsos_draw_text(win, 180, 508, "Press ESC to exit - System Info",
                     FUNSOS_COLOR_DARK_GRAY);

    /* 事件循环 */
    funsos_event_t event;
    while (1) {
        if (funsos_wait_event(&event) != 0)
            continue;
        if (event.type == FUNSOS_EVENT_KEY_PRESS && event.key == 0x1B)
            break;
        if (event.type == FUNSOS_EVENT_WINDOW_CLOSE)
            break;
    }

    funsos_destroy_window(win);
    return 0;
}
