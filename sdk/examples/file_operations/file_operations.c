/* file_operations.c - 文件操作示例程序
 * 演示 FUNSOS 中的各种文件系统操作，包括文件读写、目录操作等。
 *
 * 功能说明：
 *   - 创建和写入文件
 *   - 读取文件内容
 *   - 目录创建和遍历
 *   - 文件状态查询
 *   - 文件重命名和删除
 *
 * 使用的主要 API：
 *   - funsos_file_open() - 打开文件
 *   - funsos_file_read() - 读取文件
 *   - funsos_file_write() - 写入文件
 *   - funsos_file_close() - 关闭文件
 *   - funsos_file_mkdir() - 创建目录
 *   - funsos_file_stat() - 获取文件状态
 *   - funsos_file_rename() - 重命名文件
 *   - funsos_file_remove() - 删除文件
 */

#include "funsos.h"

#define TEST_FILE     "/tmp/test_file.txt"
#define TEST_FILE_2   "/tmp/renamed_file.txt"
#define TEST_DIR      "/tmp/test_dir"

/* 在窗口中显示文本的辅助函数 */
static void draw_status_line(funsos_window_t win, int y, const char *label,
                             const char *value, funsos_color_t color)
{
    funsos_draw_text(win, 20, y, label, FUNSOS_COLOR_BLACK);
    funsos_draw_text(win, 150, y, value, color);
}

int main(void)
{
    int fd;
    int ret;
    int line_y = 20;
    char cwd[256];
    funsos_stat_t stat_buf;

    /* 创建窗口 */
    funsos_window_t win = funsos_create_window(80, 60, 550, 450, "File Operations Demo");
    funsos_fill_window(win, FUNSOS_COLOR_WHITE);

    /* 标题 */
    funsos_draw_text(win, 20, line_y, "=== File Operations Demo ===", FUNSOS_COLOR_BLUE);
    line_y += 30;

    /* --- 1. 获取当前工作目录 --- */
    ret = funsos_file_getcwd(cwd, sizeof(cwd));
    if (ret == 0) {
        draw_status_line(win, line_y, "Current dir:", cwd, FUNSOS_COLOR_GREEN);
    } else {
        draw_status_line(win, line_y, "Current dir:", "ERROR", FUNSOS_COLOR_RED);
    }
    line_y += 25;

    /* --- 2. 创建目录 --- */
    ret = funsos_file_mkdir(TEST_DIR);
    if (ret == 0) {
        draw_status_line(win, line_y, "Mkdir:", "OK", FUNSOS_COLOR_GREEN);
    } else {
        draw_status_line(win, line_y, "Mkdir:", "Failed (may exist)", FUNSOS_COLOR_ORANGE);
    }
    line_y += 25;

    /* --- 3. 创建并写入文件 --- */
    fd = funsos_file_open(TEST_FILE, FUNSOS_O_CREAT | FUNSOS_O_WRONLY | FUNSOS_O_TRUNC);
    if (fd >= 0) {
        const char *data = "Hello from FUNSOS File I/O!\n"
                           "This is line 2 of the test file.\n"
                           "Line 3: SDK version is ";
        const char *ver = FUNSOS_SDK_VERSION;
        const char *end = "\nLine 4: End of file.\n";

        funsos_file_write(fd, data, strlen(data));
        funsos_file_write(fd, ver, strlen(ver));
        funsos_file_write(fd, end, strlen(end));
        funsos_file_close(fd);
        draw_status_line(win, line_y, "Write file:", "OK", FUNSOS_COLOR_GREEN);
    } else {
        draw_status_line(win, line_y, "Write file:", "FAILED", FUNSOS_COLOR_RED);
    }
    line_y += 25;

    /* --- 4. 获取文件状态 --- */
    ret = funsos_file_stat(TEST_FILE, &stat_buf);
    if (ret == 0) {
        char size_buf[32];
        /* 将文件大小转换为字符串 */
        int size = stat_buf.st_size;
        int pos = 0;
        if (size == 0) {
            size_buf[pos++] = '0';
        } else {
            char tmp[16];
            int tpos = 0;
            while (size > 0 && tpos < 15) {
                tmp[tpos++] = '0' + (size % 10);
                size /= 10;
            }
            for (int i = tpos - 1; i >= 0; i--) {
                size_buf[pos++] = tmp[i];
            }
        }
        size_buf[pos] = '\0';
        strcat(size_buf, " bytes");
        draw_status_line(win, line_y, "File size:", size_buf, FUNSOS_COLOR_GREEN);
    } else {
        draw_status_line(win, line_y, "File size:", "ERROR", FUNSOS_COLOR_RED);
    }
    line_y += 25;

    /* --- 5. 读取文件内容 --- */
    fd = funsos_file_open(TEST_FILE, FUNSOS_O_RDONLY);
    if (fd >= 0) {
        char buf[512];
        int bytes_read = funsos_file_read(fd, buf, sizeof(buf) - 1);
        buf[bytes_read] = '\0';
        funsos_file_close(fd);

        draw_status_line(win, line_y, "Read file:", "OK", FUNSOS_COLOR_GREEN);
        line_y += 25;

        /* 显示前几行内容 */
        funsos_draw_text(win, 20, line_y, "Content:", FUNSOS_COLOR_BLACK);
        line_y += 20;

        /* 逐行显示文件内容（最多显示4行） */
        char *start = buf;
        int line_count = 0;
        while (*start && line_count < 4) {
            char line_buf[128];
            int lpos = 0;
            while (*start && *start != '\n' && lpos < 127) {
                line_buf[lpos++] = *start++;
            }
            line_buf[lpos] = '\0';
            if (*start == '\n') start++;

            funsos_draw_text(win, 40, line_y, line_buf, FUNSOS_COLOR_DARK_GRAY);
            line_y += 20;
            line_count++;
        }
    } else {
        draw_status_line(win, line_y, "Read file:", "FAILED", FUNSOS_COLOR_RED);
    }
    line_y += 10;

    /* --- 6. 重命名文件 --- */
    ret = funsos_file_rename(TEST_FILE, TEST_FILE_2);
    if (ret == 0) {
        draw_status_line(win, line_y, "Rename:", "OK", FUNSOS_COLOR_GREEN);
    } else {
        draw_status_line(win, line_y, "Rename:", "FAILED", FUNSOS_COLOR_RED);
    }
    line_y += 25;

    /* --- 7. 检查文件是否存在 --- */
    if (funsos_file_exists(TEST_FILE_2)) {
        draw_status_line(win, line_y, "Exists check:", "Yes", FUNSOS_COLOR_GREEN);
    } else {
        draw_status_line(win, line_y, "Exists check:", "No", FUNSOS_COLOR_RED);
    }
    line_y += 25;

    /* --- 8. 删除文件 --- */
    ret = funsos_file_remove(TEST_FILE_2);
    if (ret == 0) {
        draw_status_line(win, line_y, "Delete file:", "OK", FUNSOS_COLOR_GREEN);
    } else {
        draw_status_line(win, line_y, "Delete file:", "FAILED", FUNSOS_COLOR_RED);
    }
    line_y += 25;

    /* --- 9. 检查文件是否已删除 --- */
    if (!funsos_file_exists(TEST_FILE_2)) {
        draw_status_line(win, line_y, "Deleted check:", "Yes (gone)", FUNSOS_COLOR_GREEN);
    } else {
        draw_status_line(win, line_y, "Deleted check:", "No", FUNSOS_COLOR_RED);
    }

    /* 底部提示 */
    funsos_draw_line(win, 20, 410, 530, 410, FUNSOS_COLOR_GRAY);
    funsos_draw_text(win, 120, 425, "Press ESC to exit - File I/O Demo",
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
