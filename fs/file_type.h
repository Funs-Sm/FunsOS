#ifndef FILE_TYPE_H
#define FILE_TYPE_H

#include "stdint.h"

typedef enum {
    FT_DIR = 0,
    FT_REGULAR,
    FT_TEXT,
    FT_SOURCE_C,
    FT_SOURCE_CPP,
    FT_HEADER,
    FT_ASM,
    FT_PYTHON,
    FT_SHELL,
    FT_MAKEFILE,
    FT_HTML,
    FT_CSS,
    FT_JS,
    FT_JSON,
    FT_XML,
    FT_MARKDOWN,
    FT_CONFIG,
    FT_IMAGE,
    FT_AUDIO,
    FT_VIDEO,
    FT_ARCHIVE,
    FT_EXECUTABLE,
    FT_LIBRARY,
    FT_DOCUMENT,
    FT_PDF,
    FT_SYMLINK,
    FT_DEVICE,
    FT_FIFO,
    FT_SOCKET,
    FT_HIDDEN,
    FT_BACKUP,
    FT_UNKNOWN
} file_type_t;

typedef struct {
    const char *extension;
    file_type_t type;
    const char *description;
    uint8_t color_fg;
    uint8_t color_bg;
} file_type_entry_t;

file_type_t file_type_detect(const char *filename, uint32_t mode);
const char *file_type_name(file_type_t type);
const char *file_type_description(file_type_t type);
void file_type_get_color(file_type_t type, uint8_t *fg, uint8_t *bg);

void file_color_init(void);
int  file_color_enabled(void);
void file_color_enable(int enable);
void file_color_set(file_type_t type, uint8_t fg, uint8_t bg);
void file_color_reset_classic(void);
void file_color_reset_default(void);
file_type_t file_type_from_name(const char *name);
const char *file_type_short_name(file_type_t type);

#define FILE_COLOR_NAME_COUNT 20

#endif
