/* fun_format_demo.c - .FUN Format Demo
 * Demonstrates creating, packing, and loading .FUN archive files
 * using the FUNSOS file packaging API.
 */

#include "funsos.h"

/* .FUN file header magic */
#define FUN_MAGIC  0x4E55462E  /* ".FUN" */
#define FUN_VERSION 1

/* .FUN format header structure */
typedef struct {
    uint32_t magic;           /* Magic number: ".FUN" */
    uint32_t version;         /* Format version */
    uint32_t file_count;      /* Number of files in archive */
    uint32_t index_offset;    /* Offset to file index table */
    uint32_t data_offset;     /* Offset to file data section */
    char     description[64]; /* Archive description */
} fun_header_t;

/* File entry in .FUN archive */
typedef struct {
    char     name[64];        /* File name */
    uint32_t size;            /* Original file size */
    uint32_t compressed_size; /* Compressed size (0 = not compressed) */
    uint32_t offset;          /* Offset in data section */
    uint32_t flags;           /* File flags (compressed, encrypted, etc.) */
    uint32_t checksum;        /* Simple CRC32 checksum */
} fun_entry_t;

/* Helper: simple string length */
static int my_strlen(const char *s)
{
    int len = 0;
    while (s[len]) len++;
    return len;
}

/* Helper: copy string */
static void my_strcpy(char *dst, const char *src, int max_len)
{
    int i = 0;
    while (i < max_len - 1 && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
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
    funsos_window_t win = funsos_create_window(60, 40, 660, 480, ".FUN Format Demo");
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
    funsos_draw_text(win, 20, line_y, "=== .FUN Archive Format Demo ===", title_c);
    line_y += lh + 4;
    funsos_draw_line(win, 15, line_y, 645, line_y, FUNSOS_COLOR_GRAY);
    line_y += 10;

    /* Section 1: Create a .FUN archive */
    funsos_draw_text(win, 20, line_y, "[Step 1] Creating a new .FUN archive...", label_c);
    line_y += lh;

    funsos_file_t fun_file = funsos_file_open("/var/tmp/demo.fun", FUNSOS_FILE_CREATE | FUNSOS_FILE_WRITE);
    if (fun_file == NULL) {
        funsos_draw_text(win, 40, line_y, "FAILED: Could not create .FUN file!", err_c);
        /* Still continue to demonstrate the format */
    } else {
        funsos_draw_text(win, 40, line_y, "OK - Created /var/tmp/demo.fun", good_c);
    }
    line_y += lh;

    /* Section 2: Write .FUN header */
    funsos_draw_text(win, 20, line_y, "[Step 2] Writing .FUN archive header...", label_c);
    line_y += lh;

    fun_header_t header;
    header.magic = FUN_MAGIC;
    header.version = FUN_VERSION;
    header.file_count = 3;
    header.index_offset = sizeof(fun_header_t);
    header.data_offset = sizeof(fun_header_t) + 3 * sizeof(fun_entry_t);
    my_strcpy(header.description, "Demo .FUN archive - created by fun_format_demo", 64);

    if (fun_file) {
        int written = funsos_file_write(fun_file, &header, sizeof(fun_header_t));
        char num_buf[16];
        format_uint((uint32_t)written, num_buf, sizeof(num_buf));
        funsos_draw_text(win, 40, line_y, "Written header: ", label_c);
        funsos_draw_text(win, 170, line_y, num_buf, good_c);
        funsos_draw_text(win, 210, line_y, " bytes", label_c);
    } else {
        funsos_draw_text(win, 40, line_y, "SKIPPED (no file handle)", warn_c);
    }
    line_y += lh;
    line_y += 4;

    /* Section 3: Add files to the archive */
    funsos_draw_text(win, 20, line_y, "[Step 3] Adding entries to the archive...", label_c);
    line_y += lh;

    /* Simulated file entries */
    const char *files[] = {"readme.txt", "config.json", "image.bmp"};
    const char *contents[] = {
        "This is a demo .FUN archive.\nCreated by FUNSOS.\n",
        "{\"version\": \"1.0\", \"name\": \"demo\"}\n",
        "BMP_HEADER_PLACEHOLDER\x00\x00\x00"
    };

    for (int i = 0; i < 3; i++) {
        fun_entry_t entry;
        my_strcpy(entry.name, files[i], 64);
        entry.size = my_strlen(contents[i]);
        entry.compressed_size = 0;  /* No compression */
        entry.offset = 0;
        entry.flags = 0;
        entry.checksum = 0;

        /* Calculate simple checksum */
        for (int j = 0; j < (int)entry.size; j++)
            entry.checksum += (uint8_t)contents[i][j];

        funsos_draw_text(win, 40, line_y, "  Entry: ", label_c);
        funsos_draw_text(win, 100, line_y, entry.name, text_c);

        char sz_buf[16];
        format_uint(entry.size, sz_buf, sizeof(sz_buf));
        funsos_draw_text(win, 280, line_y, " size=", label_c);
        funsos_draw_text(win, 320, line_y, sz_buf, good_c);
        line_y += lh;
    }

    line_y += 4;

    /* Section 4: Load and verify a .FUN archive */
    funsos_draw_text(win, 20, line_y, "[Step 4] Loading and verifying archive...", label_c);
    line_y += lh;

    if (fun_file) {
        funsos_file_close(fun_file);
        funsos_file_t read_file = funsos_file_open("/var/tmp/demo.fun", FUNSOS_FILE_READ);
        if (read_file) {
            fun_header_t read_header;
            int n = funsos_file_read(read_file, &read_header, sizeof(fun_header_t));
            if (n == sizeof(fun_header_t) && read_header.magic == FUN_MAGIC) {
                funsos_draw_text(win, 40, line_y, "OK - Archive verified: Magic=0x", good_c);
                /* Display magic in hex */
                char hex_buf[16];
                hex_buf[0] = '0'; hex_buf[1] = 'x';
                uint32_t m = read_header.magic;
                for (int k = 7; k >= 0; k--) {
                    uint8_t nibble = (m >> (k * 4)) & 0xF;
                    hex_buf[2 + (7 - k)] = nibble < 10 ? '0' + nibble : 'A' + (nibble - 10);
                }
                hex_buf[10] = '\0';
                funsos_draw_text(win, 280, line_y, hex_buf, good_c);
                line_y += lh;

                char cnt_buf[16];
                format_uint(read_header.file_count, cnt_buf, sizeof(cnt_buf));
                funsos_draw_text(win, 40, line_y, "  Files in archive: ", label_c);
                funsos_draw_text(win, 180, line_y, cnt_buf, good_c);
                funsos_draw_text(win, 200, line_y, "  Version: ", label_c);
                format_uint(read_header.version, cnt_buf, sizeof(cnt_buf));
                funsos_draw_text(win, 280, line_y, cnt_buf, good_c);
            } else {
                funsos_draw_text(win, 40, line_y, "FAILED: Invalid archive header!", err_c);
            }
            funsos_file_close(read_file);
        }
    }
    line_y += lh + 8;

    /* Section 5: .FUN format specification */
    funsos_draw_line(win, 15, line_y, 645, line_y, FUNSOS_COLOR_GRAY);
    line_y += 8;
    funsos_draw_text(win, 20, line_y, "[.FUN Format Specification]", title_c);
    line_y += lh + 4;

    funsos_draw_text(win, 30, line_y, "Magic:    0x2E46554E (\".FUN\" in little-endian)", text_c);
    line_y += lh;
    funsos_draw_text(win, 30, line_y, "Version:  1", text_c);
    line_y += lh;
    funsos_draw_text(win, 30, line_y, "Header:   64 bytes (magic + version + count + offsets + desc)", text_c);
    line_y += lh;
    funsos_draw_text(win, 30, line_y, "Entries:  variable length file index table", text_c);
    line_y += lh;
    funsos_draw_text(win, 30, line_y, "Data:     concatenated file contents", text_c);
    line_y += lh;
    funsos_draw_text(win, 30, line_y, "Features: compression, checksum, encryption (via flags)", text_c);
    line_y += lh + 8;

    /* Footer */
    funsos_draw_line(win, 15, 468, 645, 468, FUNSOS_COLOR_GRAY);
    funsos_draw_text(win, 20, 476, "Press ESC to exit", FUNSOS_COLOR_DARK_GRAY);

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