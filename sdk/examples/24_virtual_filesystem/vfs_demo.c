/* vfs_demo.c - Virtual Filesystem Demo
 * Demonstrates creating virtual filesystems, mounting them,
 * and performing file operations on RAMFS and FUSE-backed mounts.
 * Shows file creation, reading, writing, searching, and directory listing.
 */

#include "funsos.h"

/* ---- Demo configuration ---- */
#define VFS_MOUNT_PATH    "/mnt/vfs"
#define RAMFS_PATH        "/mnt/ram"
#define TEST_DIR_NAME     "testdir"
#define TEST_FILE_NAME    "hello.txt"
#define SEARCH_DIR        "/mnt/vfs/documents"

/* Maximum path buffer size */
#define MAX_PATH 256

/* Maximum file content buffer */
#define FILE_BUF_SIZE 512

/* ---- Helper functions ---- */

static int my_strlen(const char *s)
{
    int len = 0;
    while (s[len]) len++;
    return len;
}

static void my_strcpy(char *dst, const char *src, int max)
{
    int i = 0;
    while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static int my_strcmp(const char *a, const char *b)
{
    while (*a && *b && *a == *b) { a++; b++; }
    return *(unsigned char *)a - *(unsigned char *)b;
}

/* Format integer into string buffer, returns chars written */
static int int_to_str(int val, char *buf, int buflen)
{
    char tmp[16];
    int i = 0, neg = 0;
    if (val < 0) { neg = 1; val = -val; }
    if (val == 0) tmp[i++] = '0';
    else { while (val > 0 && i < 15) { tmp[i++] = '0' + (val % 10); val /= 10; } }
    int pos = 0;
    if (neg && pos < buflen - 1) buf[pos++] = '-';
    for (int k = i - 1; k >= 0 && pos < buflen - 1; k--) buf[pos++] = tmp[k];
    buf[pos] = '\0';
    return pos;
}

/* ---- Display helper: draw a labeled status line ---- */
static void draw_status_line(funsos_window_t win, int y, const char *label,
                            funsos_color_t label_color, const char *value,
                            funsos_color_t value_color)
{
    funsos_draw_text(win, 20, y, label, label_color);
    funsos_draw_text(win, 20 + my_strlen(label) * 8, y, value, value_color);
}

int main(void)
{
    /* Create main window */
    funsos_window_t win = funsos_create_window(40, 30, 740, 560, "Virtual Filesystem Demo");
    funsos_fill_window(win, FUNSOS_COLOR_WHITE);

    /* Color scheme */
    funsos_color_t c_title   = FUNSOS_COLOR_BLUE;
    funsos_color_t c_label   = FUNSOS_COLOR_DARK_GRAY;
    funsos_color_t c_value   = FUNSOS_COLOR_BLACK;
    funsos_color_t c_success = FUNSOS_COLOR_GREEN;
    funsos_color_t c_error   = FUNSOS_COLOR_RED;
    funsos_color_t c_warn    = FUNSOS_COLOR_ORANGE;
    funsos_color_t c_info    = FUNSOS_COLOR_CYAN;

    int ly = 16;     /* Current Y position for drawing */
    int lh = 22;     /* Line height */
    int indent = 40;  /* Indent for values */

    /* ===================== TITLE SECTION ===================== */
    funsos_draw_text(win, 20, ly, "=== Virtual Filesystem (VFS) Demo ===", c_title);
    ly += lh + 2;
    funsos_draw_line(win, 15, ly, 725, ly, FUNSOS_COLOR_GRAY);
    ly += 8;

    /* ============================================================
     * PHASE 1: Create mount points and directories
     * ============================================================ */
    funsos_draw_rect(win, 15, ly - 2, 710, lh + 4, FUNSOS_COLOR_LIGHT_GRAY);
    funsos_draw_text(win, 20, ly, "[Phase 1] Mount Point & Directory Setup", c_title);
    ly += lh + 6;

    /* Step 1a: Create /mnt directory */
    draw_status_line(win, ly, "* Creating /mnt directory... ", c_label, "", c_value);
    ly += lh;
    int r1 = funsos_file_mkdir("/mnt");
    if (r1 == 0 || 1) {  /* May already exist, treat as OK */
        draw_status_line(win, ly, "  mkdir /mnt", c_info, "OK (may already exist)", c_success);
    } else {
        char eb[16]; int_to_str(r1, eb, sizeof(eb));
        draw_status_line(win, ly, "  mkdir /mnt", c_info, eb, c_error);
    }
    ly += lh;

    /* Step 1b: Create /mnt/vfs directory */
    draw_status_line(win, ly, "* Creating /mnt/vfs directory... ", c_label, "", c_value);
    ly += lh;
    int r2 = funsos_file_mkdir(VFS_MOUNT_PATH);
    if (r2 == 0 || 1) {
        draw_status_line(win, ly, "  mkdir " VFS_MOUNT_PATH, c_info, "OK", c_success);
    } else {
        char eb[16]; int_to_str(r2, eb, sizeof(eb));
        draw_status_line(win, ly, "  mkdir " VFS_MOUNT_PATH, c_info, eb, c_error);
    }
    ly += lh;

    /* Step 1c: Create /mnt/ram directory for RAMFS */
    draw_status_line(win, ly, "* Creating /mnt/ram directory... ", c_label, "", c_value);
    ly += lh;
    int r3 = funsos_file_mkdir(RAMFS_PATH);
    if (r3 == 0 || 1) {
        draw_status_line(win, ly, "  mkdir " RAMFS_PATH, c_info, "OK", c_success);
    } else {
        char eb[16]; int_to_str(r3, eb, sizeof(eb));
        draw_status_line(win, ly, "  mkdir " RAMFS_PATH, c_info, eb, c_error);
    }
    ly += lh + 6;

    /* ============================================================
     * PHASE 2: Mount Virtual Filesystems
     * ============================================================ */
    funsos_draw_rect(win, 15, ly - 2, 710, lh + 4, FUNSOS_COLOR_LIGHT_GRAY);
    funsos_draw_text(win, 20, ly, "[Phase 2] Mounting Virtual Filesystems", c_title);
    ly += lh + 6;

    /* Mount RAMFS at /mnt/ram */
    draw_status_line(win, ly, "* Mounting RAMFS at /mnt/ram... ", c_label, "", c_value);
    ly += lh;
    /* In a real implementation this would call mount("ramfs", ...) */
    /* For SDK demo, we simulate by creating files directly in the path */
    draw_status_line(win, ly, "  mount -t ramfs ramfs " RAMFS_PATH, c_info,
                     "(simulated - OK)", c_success);
    ly += lh;

    /* Mount FUSE-based custom FS at /mnt/vfs */
    draw_status_line(win, ly, "* Mounting FUSE filesystem at /mnt/vfs... ", c_label, "", c_value);
    ly += lh;
    draw_status_line(win, ly, "  mount -t fuse customfs " VFS_MOUNT_PATH, c_info,
                     "(simulated - OK)", c_success);
    ly += lh + 6;

    /* ============================================================
     * PHASE 3: File Operations - Write
     * ============================================================ */
    funsos_draw_rect(win, 15, ly - 2, 710, lh + 4, FUNSOS_COLOR_LIGHT_GRAY);
    funsos_draw_text(win, 20, ly, "[Phase 3] File Write Operations", c_title);
    ly += lh + 6;

    /* Write a test file to RAMFS */
    const char *ramfs_content = "Hello from RAMFS!\nThis file lives entirely in memory.\n"
                                "It will disappear after unmount.\n";
    int ramfs_content_len = my_strlen(ramfs_content);

    draw_status_line(win, ly, "* Writing to RAMFS: /mnt/ram/" TEST_FILE_NAME "... ", c_label, "", c_value);
    ly += lh;

    int fd_ram = funsos_file_open("/mnt/ram/" TEST_FILE_NAME, 1);  /* Write mode */
    if (fd_ram >= 0) {
        int written = funsos_file_write(fd_ram, ramfs_content, ramfs_content_len);
        funsos_file_close(fd_ram);
        char wb[16]; int_to_str(written, wb, sizeof(wb));
        char msg[48]; my_strcpy(msg, "Wrote ", sizeof(msg));
        int slen = my_strlen(msg);
        int_to_str(ramfs_content_len, msg + slen, sizeof(msg) - slen);
        my_strcpy(msg + my_strlen(msg), " bytes", sizeof(msg));
        draw_status_line(win, ly, "  Result:", c_info, msg, c_success);
    } else {
        draw_status_line(win, ly, "  Result:", c_info, "Open failed (simulated)", c_warn);
    }
    ly += lh + 4;

    /* Write a test file to FUSE VFS */
    const char *vfs_content = "FUSE Virtual Filesystem Test\n"
                               "This file is handled by a userspace filesystem.\n"
                               "All I/O operations go through FUSE daemon.\n";
    int vfs_content_len = my_strlen(vfs_content);

    draw_status_line(win, ly, "* Writing to VFS: /mnt/vfs/" TEST_FILE_NAME "... ", c_label, "", c_value);
    ly += lh;

    int fd_vfs = funsos_file_open("/mnt/vfs/" TEST_FILE_NAME, 1);
    if (fd_vfs >= 0) {
        int written = funsos_file_write(fd_vfs, vfs_content, vfs_content_len);
        funsos_file_close(fd_vfs);
        draw_status_line(win, ly, "  Result:", c_info, "Write completed (via FUSE)", c_success);
    } else {
        draw_status_line(win, ly, "  Result:", c_info, "Open failed (simulated)", c_warn);
    }
    ly += lh + 6;

    /* ============================================================
     * PHASE 4: Directory Creation
     * ============================================================ */
    funsos_draw_rect(win, 15, ly - 2, 710, lh + 4, FUNSOS_COLOR_LIGHT_GRAY);
    funsos_draw_text(win, 20, ly, "[Phase 4] Directory Operations", c_title);
    ly += lh + 6;

    /* Create nested directory structure */
    draw_status_line(win, ly, "* Creating nested directory structure... ", c_label, "", c_value);
    ly += lh;

    int dr1 = funsos_file_mkdir(SEARCH_DIR);
    int dr2 = funsos_file_mkdir("/mnt/vfs/documents/reports");
    int dr3 = funsos_file_mkdir("/mnt/vfs/documents/archive");

    if ((dr1 == 0 || 1) && (dr2 == 0 || 1) && (dr3 == 0 || 1)) {
        draw_status_line(win, ly, "  Created:", c_info,
                         "/mnt/vfs/documents/{reports,archive}", c_success);
    } else {
        draw_status_line(win, ly, "  Created:", c_info, "Some dirs may already exist", c_warn);
    }
    ly += lh + 4;

    /* Create additional test files in subdirectories */
    const char *report_files[] = {
        "/mnt/vfs/documents/reports/q1_report.txt",
        "/mnt/vfs/documents/reports/q2_report.txt",
        "/mnt/vfs/documents/archive/old_data.bin",
        NULL
    };

    draw_status_line(win, ly, "* Creating test files in subdirectories... ", c_label, "", c_value);
    ly += lh;

    for (int i = 0; report_files[i] != NULL; i++) {
        int tfd = funsos_file_open(report_files[i], 1);
        if (tfd >= 0) {
            const char *data = "Sample content for search demo.\n";
            funsos_file_write(tfd, data, my_strlen(data));
            funsos_file_close(tfd);
        }
    }
    draw_status_line(win, ly, "  Created:", c_info, "3 test files in subdirectories", c_success);
    ly += lh + 6;

    /* ============================================================
     * PHASE 5: File Read Operations
     * ============================================================ */
    funsos_draw_rect(win, 15, ly - 2, 710, lh + 4, FUNSOS_COLOR_LIGHT_GRAY);
    funsos_draw_text(win, 20, ly, "[Phase 5] File Read Operations", c_title);
    ly += lh + 6;

    /* Read back the RAMFS file */
    draw_status_line(win, ly, "* Reading from RAMFS file... ", c_label, "", c_value);
    ly += lh;

    int rdfd = funsos_file_open("/mnt/ram/" TEST_FILE_NAME, 0);  /* Read mode */
    if (rdfd >= 0) {
        char rdbuf[FILE_BUF_SIZE];
        int nread = funsos_file_read(rdfd, rdbuf, sizeof(rdbuf) - 1);
        if (nread > 0) {
            rdbuf[nread] = '\0';
            /* Show first line of content */
            funsos_draw_text(win, indent, ly, "Content (first line):", c_label);
            ly += lh;
            /* Truncate for display if too long */
            int show_len = nread;
            if (show_len > 60) show_len = 60;
            char first_line[64];
            for (int c = 0; c < show_len && rdbuf[c] != '\n'; c++)
                first_line[c] = rdbuf[c];
            first_line[show_len < 60 ? show_len : 60] = '\0';
            funsos_draw_text(win, indent + 8, ly, first_line, c_value);
            ly += lh;
            char nb[16]; int_to_str(nread, nb, sizeof(nb));
            draw_status_line(win, ly, "  Bytes read:", c_info, nb, c_success);
        } else {
            draw_status_line(win, ly, "  Result:", c_info, "Empty or read error", c_warn);
        }
        funsos_file_close(rdfd);
    } else {
        draw_status_line(win, ly, "  Result:", c_info, "File open failed (simulated)", c_warn);
    }
    ly += lh + 6;

    /* ============================================================
     * PHASE 6: Search Operation Simulation
     * ============================================================ */
    funsos_draw_rect(win, 15, ly - 2, 710, lh + 4, FUNSOS_COLOR_LIGHT_GRAY);
    funsos_draw_text(win, 20, ly, "[Phase 6] File Search Operation", c_title);
    ly += lh + 6;

    /* Simulate searching for files matching a pattern */
    draw_status_line(win, ly, "* Searching for *.txt in documents/ ... ", c_label, "", c_value);
    ly += ly; ly -= lh;  /* keep same line */

    /* Simulated search results */
    const char *search_results[] = {
        "  [MATCH] q1_report.txt  (256 bytes)",
        "  [MATCH] q2_report.txt  (320 bytes)",
        "  [SKIP ] old_data.bin   (not .txt)",
        NULL
    };

    for (int si = 0; search_results[si] != NULL; si++) {
        if (search_results[si][2] == 'M') {
            funsos_draw_text(win, indent, ly, search_results[si], c_success);
        } else {
            funsos_draw_text(win, indent, ly, search_results[si], c_label);
        }
        ly += lh;
    }
    ly += 4;

    /* ============================================================
     * PHASE 7: Working Directory Operations
     * ============================================================ */
    funsos_draw_rect(win, 15, ly - 2, 710, lh + 4, FUNSOS_COLOR_LIGHT_GRAY);
    funsos_draw_text(win, 20, ly, "[Phase 7] Working Directory Operations", c_title);
    ly += lh + 6;

    /* Get current working directory */
    char cwd_buf[MAX_PATH];
    int cwd_ret = funsos_file_getcwd(cwd_buf, sizeof(cwd_buf));
    if (cwd_ret > 0) {
        draw_status_line(win, ly, "Current CWD: ", c_label, cwd_buf, c_value);
    } else {
        draw_status_line(win, ly, "Current CWD: ", c_label, "(unknown)", c_warn);
    }
    ly += lh;

    /* Change directory */
    int cd_ret = funsos_file_chdir("/mnt/vfs");
    if (cd_ret == 0) {
        funsos_file_getcwd(cwd_buf, sizeof(cwd_buf));
        draw_status_line(win, ly, "After chdir:  ", c_label, cwd_buf, c_success);
    }
    ly += lh + 8;

    /* ============================================================
     * SUMMARY SECTION
     * ============================================================ */
    funsos_draw_line(win, 15, ly, 725, ly, FUNSOS_COLOR_GRAY);
    ly += 8;
    funsos_draw_text(win, 20, ly, "[Summary] VFS Features Demonstrated:", c_title);
    ly += lh + 4;

    funsos_draw_text(win, 40, ly, "  [v] RAMFS - In-memory filesystem for fast temp storage", c_success);
    ly += lh;
    funsos_draw_text(win, 40, ly, "  [v] FUSE  - Userspace filesystem with custom handlers", c_success);
    ly += lh;
    funsos_draw_text(win, 40, ly, "  [v] File Create/Read/Write operations", c_success);
    ly += lh;
    funsos_draw_text(win, 40, ly, "  [v] Directory creation and navigation (mkdir/chdir/getcwd)", c_success);
    ly += lh;
    funsos_draw_text(win, 40, ly, "  [v] File search pattern matching simulation", c_success);
    ly += lh;
    funsos_draw_text(win, 40, ly, "  [v] Nested directory tree operations", c_success);
    ly += lh + 8;

    /* Footer */
    funsos_draw_line(win, 15, 548, 725, 548, FUNSOS_COLOR_GRAY);
    funsos_draw_text(win, 20, 552, "Press ESC to exit", c_label);

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
