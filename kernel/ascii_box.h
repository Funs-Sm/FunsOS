#ifndef ASCII_BOX_H
#define ASCII_BOX_H

#include "stdint.h"

int ascii_draw_box(char *buf, int width, int height, const char *title);
int ascii_draw_double_box(char *buf, int width, int height, const char *title);
int ascii_draw_separator(char *buf, int width);
int ascii_draw_header(char *buf, int width, const char *text);
int ascii_format_table(char *buf, int col_count, int *col_widths, char **headers, int row_count, char ***rows);
int ascii_progress_bar(char *buf, int width, int percent);

#endif
