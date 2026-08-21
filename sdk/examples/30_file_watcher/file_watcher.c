/* file_watcher.c - 文件监控示例
 * 演示 FUNSOS SDK v1.5.0 inotify 文件监控功能。
 */

#include "funsos.h"
#include "funsos_fs.h"

#define WATCH_DIR "/tmp/watch_dir"

static int g_event_count = 0;
static char g_event_log[20][128];

static void add_log(const char *msg)
{
    if (g_event_count < 20) {
        int i = 0;
        while (msg[i] && i < 127) {
            g_event_log[g_event_count][i] = msg[i];
            i++;
        }
        g_event_log[g_event_count][i] = '\0';
        g_event_count++;
    }
}

int main(void)
{
    funsos_window_t win = funsos_create_window(100, 80, 600, 450, "文件监控示例 v1.5.0");
    funsos_fill_window(win, 0xFFFFFF);

    funsos_color_t black = {0x00, 0x00, 0x00, 0xFF};
    funsos_color_t blue  = {0x00, 0x00, 0xFF, 0xFF};
    funsos_color_t green = {0x00, 0x80, 0x00, 0xFF};
    funsos_color_t red   = {0xFF, 0x00, 0x00, 0xFF};
    funsos_color_t orange = {0xFF, 0x80, 0x00, 0xFF};

    funsos_draw_text(win, 20, 20, "FUNSOS SDK v1.5.0 - 文件监控示例", blue);
    funsos_draw_text(win, 20, 45, "使用 inotify API 监控文件系统变化", black);

    /* 创建监控目录 */
    funsos_file_mkdir(WATCH_DIR);
    add_log("[INIT] 创建监控目录");

    /* 初始化 inotify */
    int ifd = funsos_inotify_init();
    if (ifd < 0) {
        funsos_draw_text(win, 20, 80, "[ERR] inotify 初始化失败", red);
        add_log("[ERR] inotify_init 失败");
    } else {
        funsos_draw_text(win, 20, 80, "[OK] inotify 初始化成功", green);
        add_log("[OK] inotify_init 成功");
    }

    /* 添加监控 */
    int wd = -1;
    if (ifd >= 0) {
        wd = funsos_inotify_add_watch(ifd, WATCH_DIR,
            FUNSOS_IN_CREATE | FUNSOS_IN_DELETE | FUNSOS_IN_MODIFY);
        if (wd < 0) {
            funsos_draw_text(win, 20, 105, "[ERR] 添加监控失败", red);
            add_log("[ERR] inotify_add_watch 失败");
        } else {
            funsos_draw_text(win, 20, 105, "[OK] 监控目录已添加", green);
            add_log("[OK] 添加目录监控");
        }
    }

    /* 模拟一些文件操作 */
    char filepath[256];
    funsos_snprintf(filepath, sizeof(filepath), "%s/test1.txt", WATCH_DIR);

    /* 创建文件 */
    int fd = funsos_file_open(filepath, FUNSOS_O_WRONLY | FUNSOS_O_CREAT | FUNSOS_O_TRUNC);
    if (fd >= 0) {
        funsos_file_write(fd, "Hello, FUNSOS!\n", 14);
        funsos_file_close(fd);
        add_log("[EVENT] 创建文件 test1.txt");
    }

    /* 修改文件 */
    fd = funsos_file_open(filepath, FUNSOS_O_WRONLY | FUNSOS_O_APPEND);
    if (fd >= 0) {
        funsos_file_write(fd, "File modified!\n", 15);
        funsos_file_close(fd);
        add_log("[EVENT] 修改文件 test1.txt");
    }

    /* 创建第二个文件 */
    char filepath2[256];
    funsos_snprintf(filepath2, sizeof(filepath2), "%s/test2.txt", WATCH_DIR);
    fd = funsos_file_open(filepath2, FUNSOS_O_WRONLY | FUNSOS_O_CREAT | FUNSOS_O_TRUNC);
    if (fd >= 0) {
        funsos_file_write(fd, "Second file\n", 12);
        funsos_file_close(fd);
        add_log("[EVENT] 创建文件 test2.txt");
    }

    /* 删除第一个文件 */
    funsos_file_remove(filepath);
    add_log("[EVENT] 删除文件 test1.txt");

    /* 显示支持的监控事件类型 */
    funsos_draw_text(win, 20, 140, "支持的监控事件类型:", blue);
    funsos_draw_text(win, 40, 165, "IN_ACCESS   - 文件被访问", black);
    funsos_draw_text(win, 40, 185, "IN_MODIFY   - 文件被修改", black);
    funsos_draw_text(win, 40, 205, "IN_ATTRIB   - 属性变化", black);
    funsos_draw_text(win, 40, 225, "IN_CREATE   - 文件创建", black);
    funsos_draw_text(win, 40, 245, "IN_DELETE   - 文件删除", black);
    funsos_draw_text(win, 40, 265, "IN_MOVED_FROM/TO - 文件移动", black);

    /* 显示事件日志 */
    funsos_draw_text(win, 320, 140, "事件日志:", blue);
    for (int i = 0; i < g_event_count && i < 12; i++) {
        funsos_draw_text(win, 320, 165 + i * 20, g_event_log[i], orange);
    }

    /* 清理 */
    if (wd >= 0) {
        funsos_inotify_rm_watch(ifd, wd);
    }
    if (ifd >= 0) {
        funsos_file_close(ifd);
    }

    funsos_draw_text(win, 20, 410, "按 ESC 退出", black);

    /* 事件循环 */
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
