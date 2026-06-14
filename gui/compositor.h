#ifndef COMPOSITOR_H
#define COMPOSITOR_H

#include "gfx.h"
#include "wm.h"

void compositor_init(uint32_t *fb, uint32_t w, uint32_t h);
void compositor_render(void);
void compositor_mark_dirty(gfx_rect_t rect);
void compositor_swap_buffers(void);
gfx_context_t *compositor_get_back_buffer(void);

#endif
