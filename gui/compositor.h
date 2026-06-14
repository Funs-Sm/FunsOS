#ifndef COMPOSITOR_H
#define COMPOSITOR_H

#include "gfx.h"
#include "wm.h"

void compositor_init(uint32_t *fb, uint32_t w, uint32_t h);
void compositor_render(void);
void compositor_mark_dirty(gfx_rect_t rect);
void compositor_swap_buffers(void);
gfx_context_t *compositor_get_back_buffer(void);

/* ---- Bridge to FunRender rendering pipeline ---- */
typedef void *fr_render_handle_t;

void compositor_set_fr_context(fr_render_handle_t fr_ctx);
fr_render_handle_t compositor_get_fr_context(void);
int compositor_render_to_funrender(fr_render_handle_t fr_ctx);
void compositor_sync_with_funrender(void);

#endif
