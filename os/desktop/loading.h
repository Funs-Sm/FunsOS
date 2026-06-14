#ifndef LOADING_H
#define LOADING_H

#include "stdint.h"

/* 加载动画状态 */
#define LOADING_SPINNER_FRAMES   8
#define LOADING_SPINNER_RADIUS   24
#define LOADING_SPINNER_WIDTH    4
#define LOADING_PARTICLE_COUNT   20
#define LOADING_DOT_SPACING      8
#define LOADING_MAX_DOTS         8

void loading_screen_init(int w, int h, uint32_t *fb, uint32_t pitch);
void loading_screen_render_frame(void);
void loading_screen_set_progress(const char *msg, int pct);
int  loading_screen_is_done(void);
void loading_screen_mark_done(void);

#endif