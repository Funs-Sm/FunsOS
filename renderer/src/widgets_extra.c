/* fr_widgets_extra.c - FUNSOS 渲染器扩展控件实现
 * 树形视图、标签页、进度条、滑块、滚动条、菜单等控件
 */

#include "fr_widgets_extra.h"
#include "fr_context.h"
#include "funrender.h"
#include "gfx.h"
#include "string.h"

/* 将 fr_context_t 转换为 gfx_context_t 以便使用 gfx 函数 */
static void fr_ctx_to_gfx(fr_context_t *src, gfx_context_t *dst) {
    if (src == NULL || dst == NULL) return;
    gfx_init(dst, src->framebuffer, (uint32_t)src->width, (uint32_t)src->height,
             (uint32_t)src->pitch, (uint32_t)src->bpp);
}

/* ================================================================
 *  树形视图
 * ================================================================ */

fr_tree_ex_t *fr_tree_create(fr_rect_t bounds, fr_tree_node_ex_t *root)
{
    fr_tree_ex_t *tree = (fr_tree_ex_t *)fr_alloc(sizeof(fr_tree_ex_t));
    if (tree == NULL) return NULL;

    memset(tree, 0, sizeof(fr_tree_ex_t));
    tree->bounds = bounds;
    tree->root = root;
    tree->item_height = 20;
    tree->indent_width = 16;
    tree->show_lines = 1;
    tree->visible = 1;
    return tree;
}

void fr_tree_destroy(fr_tree_ex_t *tree)
{
    if (tree == NULL) return;
    fr_free(tree);
}

fr_tree_node_ex_t *fr_tree_add_node(fr_tree_node_ex_t *parent, const char *label, void *data)
{
    fr_tree_node_ex_t *node = (fr_tree_node_ex_t *)fr_alloc(sizeof(fr_tree_node_ex_t));
    if (node == NULL) return NULL;

    memset(node, 0, sizeof(fr_tree_node_ex_t));
    node->label = (char *)fr_alloc(strlen(label) + 1);
    if (node->label) {
        memcpy(node->label, label, strlen(label) + 1);
    }
    node->user_data = data;
    node->parent = parent;

    if (parent) {
        parent->has_children = 1;
        parent->child_count++;
        if (parent->first_child == NULL) {
            parent->first_child = node;
        } else {
            fr_tree_node_ex_t *sibling = parent->first_child;
            while (sibling->next_sibling) {
                sibling = sibling->next_sibling;
            }
            sibling->next_sibling = node;
            node->prev_sibling = sibling;
        }
    }

    return node;
}

void fr_tree_remove_node(fr_tree_node_ex_t *node)
{
    if (node == NULL) return;
    /* 递归移除子节点 */
    fr_tree_node_ex_t *child = node->first_child;
    while (child) {
        fr_tree_node_ex_t *next = child->next_sibling;
        fr_tree_remove_node(child);
        child = next;
    }
    if (node->label) fr_free(node->label);
    fr_free(node);
}

void fr_tree_expand(fr_tree_node_ex_t *node)
{
    if (node) node->expanded = 1;
}

void fr_tree_collapse(fr_tree_node_ex_t *node)
{
    if (node) node->expanded = 0;
}

void fr_tree_select(fr_tree_ex_t *tree, fr_tree_node_ex_t *node)
{
    if (tree == NULL) return;
    tree->selected = node;
    if (tree->on_select) {
        tree->on_select(node);
    }
}

fr_tree_node_ex_t *fr_tree_get_selected(fr_tree_ex_t *tree)
{
    return tree ? tree->selected : NULL;
}

void fr_tree_render(fr_tree_ex_t *tree, fr_context_t *ctx)
{
    if (tree == NULL || ctx == NULL || !tree->visible) return;
    gfx_context_t gfx_ctx;
    fr_ctx_to_gfx(ctx, &gfx_ctx);
    gfx_draw_rect(&gfx_ctx, (gfx_rect_t){(int32_t)tree->bounds.x, (int32_t)tree->bounds.y,
                  (int32_t)tree->bounds.w, (int32_t)tree->bounds.h}, 0xCCCCCC);
}

int fr_tree_handle_event(fr_tree_ex_t *tree, fr_event_t *event)
{
    if (tree == NULL || event == NULL) return 0;
    return 0;
}

/* ================================================================
 *  标签页
 * ================================================================ */

fr_tabcontrol_ex_t *fr_tabcontrol_create(fr_rect_t bounds, uint32_t max_tabs)
{
    fr_tabcontrol_ex_t *tab = (fr_tabcontrol_ex_t *)fr_alloc(sizeof(fr_tabcontrol_ex_t));
    if (tab == NULL) return NULL;

    memset(tab, 0, sizeof(fr_tabcontrol_ex_t));
    tab->bounds = bounds;
    tab->max_tabs = max_tabs;
    tab->tab_height = 28;
    tab->bg_color = 0xF0F0F0;
    tab->active_color = 0xFFFFFF;
    tab->inactive_color = 0xD0D0D0;
    tab->text_color = 0x000000;
    tab->visible = 1;

    tab->tabs = (fr_tab_ex_t *)fr_alloc(max_tabs * sizeof(fr_tab_ex_t));
    if (tab->tabs == NULL) {
        fr_free(tab);
        return NULL;
    }
    memset(tab->tabs, 0, max_tabs * sizeof(fr_tab_ex_t));

    return tab;
}

void fr_tabcontrol_destroy(fr_tabcontrol_ex_t *tab)
{
    if (tab == NULL) return;
    if (tab->tabs) fr_free(tab->tabs);
    fr_free(tab);
}

int fr_tabcontrol_add_tab(fr_tabcontrol_ex_t *tab, const char *label, void *content, int closable)
{
    if (tab == NULL || tab->tab_count >= tab->max_tabs || label == NULL) return -1;

    fr_tab_ex_t *t = &tab->tabs[tab->tab_count];
    memset(t, 0, sizeof(fr_tab_ex_t));
    strncpy(t->label, label, FR_TAB_MAX_LABEL - 1);
    t->content = content;
    t->closable = (uint8_t)closable;

    if (tab->tab_count == 0) {
        t->active = 1;
        tab->active_index = 0;
    }

    tab->tab_count++;
    return 0;
}

int fr_tabcontrol_remove_tab(fr_tabcontrol_ex_t *tab, uint32_t index)
{
    if (tab == NULL || index >= tab->tab_count) return -1;

    /* 移动后续标签 */
    for (uint32_t i = index; i < tab->tab_count - 1; i++) {
        memcpy(&tab->tabs[i], &tab->tabs[i + 1], sizeof(fr_tab_ex_t));
    }
    memset(&tab->tabs[tab->tab_count - 1], 0, sizeof(fr_tab_ex_t));
    tab->tab_count--;

    if (tab->active_index >= tab->tab_count && tab->tab_count > 0) {
        tab->active_index = tab->tab_count - 1;
        tab->tabs[tab->active_index].active = 1;
    }

    return 0;
}

int fr_tabcontrol_set_active(fr_tabcontrol_ex_t *tab, uint32_t index)
{
    if (tab == NULL || index >= tab->tab_count) return -1;

    tab->tabs[tab->active_index].active = 0;
    tab->active_index = index;
    tab->tabs[index].active = 1;

    if (tab->on_tab_changed) {
        tab->on_tab_changed(index);
    }
    return 0;
}

uint32_t fr_tabcontrol_get_active(fr_tabcontrol_ex_t *tab)
{
    return tab ? tab->active_index : 0;
}

void *fr_tabcontrol_get_content(fr_tabcontrol_ex_t *tab)
{
    if (tab == NULL || tab->active_index >= tab->tab_count) return NULL;
    return tab->tabs[tab->active_index].content;
}

void fr_tabcontrol_render(fr_tabcontrol_ex_t *tab, fr_context_t *ctx)
{
    if (tab == NULL || ctx == NULL || !tab->visible) return;

    gfx_context_t gfx_ctx;
    fr_ctx_to_gfx(ctx, &gfx_ctx);

    /* 绘制标签栏背景 */
    gfx_draw_rect(&gfx_ctx, (gfx_rect_t){(int32_t)tab->bounds.x, (int32_t)tab->bounds.y,
                  (int32_t)tab->bounds.w, (int32_t)tab->tab_height}, tab->bg_color);

    /* 绘制标签 */
    int32_t tab_width = (int32_t)(tab->bounds.w / (tab->tab_count > 0 ? tab->tab_count : 1));
    for (uint32_t i = 0; i < tab->tab_count; i++) {
        uint32_t color = tab->tabs[i].active ? tab->active_color : tab->inactive_color;
        gfx_draw_rect(&gfx_ctx, (gfx_rect_t){(int32_t)(tab->bounds.x + i * tab_width),
                      (int32_t)tab->bounds.y, tab_width, (int32_t)tab->tab_height}, color);
    }
}

int fr_tabcontrol_handle_event(fr_tabcontrol_ex_t *tab, fr_event_t *event)
{
    if (tab == NULL || event == NULL) return 0;
    return 0;
}

/* ================================================================
 *  进度条
 * ================================================================ */

fr_progress_ex_t *fr_progress_create(fr_rect_t bounds, uint32_t min_val, uint32_t max_val)
{
    fr_progress_ex_t *progress = (fr_progress_ex_t *)fr_alloc(sizeof(fr_progress_ex_t));
    if (progress == NULL) return NULL;

    memset(progress, 0, sizeof(fr_progress_ex_t));
    progress->bounds = bounds;
    progress->min_value = min_val;
    progress->max_value = max_val;
    progress->current_value = min_val;
    progress->bg_color = 0xDDDDDD;
    progress->fill_color = 0x0078D7;
    progress->border_color = 0x999999;
    progress->percent = 0.0f;
    progress->show_percent = 0;
    progress->visible = 1;
    return progress;
}

void fr_progress_destroy(fr_progress_ex_t *progress)
{
    if (progress) fr_free(progress);
}

void fr_progress_set_value(fr_progress_ex_t *progress, uint32_t value)
{
    if (progress == NULL) return;
    if (value < progress->min_value) value = progress->min_value;
    if (value > progress->max_value) value = progress->max_value;
    progress->current_value = value;

    uint32_t range = progress->max_value - progress->min_value;
    progress->percent = (range > 0) ? (float)(value - progress->min_value) / (float)range : 0.0f;
}

uint32_t fr_progress_get_value(fr_progress_ex_t *progress)
{
    return progress ? progress->current_value : 0;
}

void fr_progress_set_percent(fr_progress_ex_t *progress, float percent)
{
    if (progress == NULL) return;
    if (percent < 0.0f) percent = 0.0f;
    if (percent > 1.0f) percent = 1.0f;
    progress->percent = percent;
    progress->current_value = progress->min_value +
        (uint32_t)(percent * (float)(progress->max_value - progress->min_value));
}

void fr_progress_increment(fr_progress_ex_t *progress, uint32_t delta)
{
    if (progress) fr_progress_set_value(progress, progress->current_value + delta);
}

void fr_progress_render(fr_progress_ex_t *progress, fr_context_t *ctx)
{
    if (progress == NULL || ctx == NULL || !progress->visible) return;

    gfx_context_t gfx_ctx;
    fr_ctx_to_gfx(ctx, &gfx_ctx);

    /* 背景 */
    gfx_draw_rect(&gfx_ctx, (gfx_rect_t){(int32_t)progress->bounds.x, (int32_t)progress->bounds.y,
                  (int32_t)progress->bounds.w, (int32_t)progress->bounds.h}, progress->bg_color);

    /* 填充 */
    int32_t fill_w = (int32_t)(progress->percent * (float)progress->bounds.w);
    if (fill_w > 0) {
        gfx_draw_rect(&gfx_ctx, (gfx_rect_t){(int32_t)progress->bounds.x + 1,
                      (int32_t)progress->bounds.y + 1, fill_w - 2,
                      (int32_t)progress->bounds.h - 2}, progress->fill_color);
    }

    /* 边框（用4条线绘制） */
    gfx_draw_line(&gfx_ctx, (int32_t)progress->bounds.x, (int32_t)progress->bounds.y,
                  (int32_t)(progress->bounds.x + progress->bounds.w), (int32_t)progress->bounds.y,
                  progress->border_color);
    gfx_draw_line(&gfx_ctx, (int32_t)progress->bounds.x,
                  (int32_t)(progress->bounds.y + progress->bounds.h),
                  (int32_t)(progress->bounds.x + progress->bounds.w),
                  (int32_t)(progress->bounds.y + progress->bounds.h),
                  progress->border_color);
}

/* ================================================================
 *  滑块
 * ================================================================ */

fr_slider_ex_t *fr_slider_create(fr_rect_t bounds, int32_t min_val, int32_t max_val, int32_t initial)
{
    fr_slider_ex_t *slider = (fr_slider_ex_t *)fr_alloc(sizeof(fr_slider_ex_t));
    if (slider == NULL) return NULL;

    memset(slider, 0, sizeof(fr_slider_ex_t));
    slider->bounds = bounds;
    slider->min_value = min_val;
    slider->max_value = max_val;
    slider->current_value = initial;
    slider->step = 1;
    slider->track_height = 4;
    slider->thumb_size = 16;
    slider->track_color = 0xCCCCCC;
    slider->fill_color = 0x0078D7;
    slider->thumb_color = 0xFFFFFF;
    slider->visible = 1;
    return slider;
}

void fr_slider_destroy(fr_slider_ex_t *slider)
{
    if (slider) fr_free(slider);
}

void fr_slider_set_value(fr_slider_ex_t *slider, int32_t value)
{
    if (slider == NULL) return;
    if (value < slider->min_value) value = slider->min_value;
    if (value > slider->max_value) value = slider->max_value;
    slider->current_value = value;
    if (slider->on_value_changed) {
        slider->on_value_changed(value);
    }
}

int32_t fr_slider_get_value(fr_slider_ex_t *slider)
{
    return slider ? slider->current_value : 0;
}

void fr_slider_render(fr_slider_ex_t *slider, fr_context_t *ctx)
{
    if (slider == NULL || ctx == NULL || !slider->visible) return;

    gfx_context_t gfx_ctx;
    fr_ctx_to_gfx(ctx, &gfx_ctx);

    int32_t track_y = (int32_t)(slider->bounds.y + slider->bounds.h / 2.0f - (float)slider->track_height / 2.0f);

    /* 轨道 */
    gfx_draw_rect(&gfx_ctx, (gfx_rect_t){(int32_t)slider->bounds.x, track_y,
                  (int32_t)slider->bounds.w, (int32_t)slider->track_height}, slider->track_color);

    /* 填充 */
    float ratio = (float)(slider->current_value - slider->min_value) /
                  (float)(slider->max_value - slider->min_value);
    int32_t fill_w = (int32_t)(ratio * (float)slider->bounds.w);
    if (fill_w > 0) {
        gfx_draw_rect(&gfx_ctx, (gfx_rect_t){(int32_t)slider->bounds.x, track_y,
                      fill_w, (int32_t)slider->track_height}, slider->fill_color);
    }

    /* 滑块 */
    int32_t thumb_x = (int32_t)(slider->bounds.x + fill_w - (int32_t)slider->thumb_size / 2);
    int32_t thumb_y = (int32_t)(slider->bounds.y + slider->bounds.h / 2.0f - (float)slider->thumb_size / 2.0f);
    gfx_draw_rect(&gfx_ctx, (gfx_rect_t){thumb_x, thumb_y,
                  (int32_t)slider->thumb_size, (int32_t)slider->thumb_size}, slider->thumb_color);
}

int fr_slider_handle_event(fr_slider_ex_t *slider, fr_event_t *event)
{
    if (slider == NULL || event == NULL) return 0;
    return 0;
}

/* ================================================================
 *  滚动条
 * ================================================================ */

fr_scrollbar_ex_t *fr_scrollbar_create(fr_rect_t bounds, uint32_t orientation)
{
    fr_scrollbar_ex_t *scrollbar = (fr_scrollbar_ex_t *)fr_alloc(sizeof(fr_scrollbar_ex_t));
    if (scrollbar == NULL) return NULL;

    memset(scrollbar, 0, sizeof(fr_scrollbar_ex_t));
    scrollbar->bounds = bounds;
    scrollbar->orientation = orientation;
    scrollbar->track_color = 0xE0E0E0;
    scrollbar->thumb_color = 0xA0A0A0;
    scrollbar->arrow_color = 0x666666;
    scrollbar->auto_hide = 1;
    scrollbar->visible = 1;
    return scrollbar;
}

void fr_scrollbar_destroy(fr_scrollbar_ex_t *scrollbar)
{
    if (scrollbar) fr_free(scrollbar);
}

void fr_scrollbar_set_range(fr_scrollbar_ex_t *scrollbar, uint32_t total, uint32_t viewport)
{
    if (scrollbar == NULL) return;
    scrollbar->total_size = total;
    scrollbar->viewport_size = viewport;

    if (scrollbar->auto_hide) {
        scrollbar->visible = (total > viewport) ? 1 : 0;
    }

    /* 计算滑块大小 */
    if (total > 0 && viewport < total) {
        uint32_t track = (scrollbar->orientation == FR_PROGRESS_HORIZONTAL) ?
                         scrollbar->bounds.w : scrollbar->bounds.h;
        scrollbar->thumb_size = (uint32_t)((uint64_t)viewport * track / total);
        if (scrollbar->thumb_size < 16) scrollbar->thumb_size = 16;
    }
}

void fr_scrollbar_set_position(fr_scrollbar_ex_t *scrollbar, uint32_t pos)
{
    if (scrollbar == NULL) return;
    if (scrollbar->total_size > scrollbar->viewport_size) {
        uint32_t max_pos = scrollbar->total_size - scrollbar->viewport_size;
        if (pos > max_pos) pos = max_pos;
    }
    scrollbar->scroll_pos = pos;
}

uint32_t fr_scrollbar_get_position(fr_scrollbar_ex_t *scrollbar)
{
    return scrollbar ? scrollbar->scroll_pos : 0;
}

void fr_scrollbar_render(fr_scrollbar_ex_t *scrollbar, fr_context_t *ctx)
{
    if (scrollbar == NULL || ctx == NULL || !scrollbar->visible) return;

    gfx_context_t gfx_ctx;
    fr_ctx_to_gfx(ctx, &gfx_ctx);

    /* 轨道 */
    gfx_draw_rect(&gfx_ctx, (gfx_rect_t){(int32_t)scrollbar->bounds.x, (int32_t)scrollbar->bounds.y,
                  (int32_t)scrollbar->bounds.w, (int32_t)scrollbar->bounds.h}, scrollbar->track_color);

    /* 滑块 */
    if (scrollbar->total_size > 0 && scrollbar->viewport_size < scrollbar->total_size) {
        uint32_t track = (scrollbar->orientation == FR_PROGRESS_HORIZONTAL) ?
                         scrollbar->bounds.w : scrollbar->bounds.h;
        float ratio = (float)scrollbar->scroll_pos /
                      (float)(scrollbar->total_size - scrollbar->viewport_size);
        int32_t offset = (int32_t)(ratio * (float)(track - scrollbar->thumb_size));

        if (scrollbar->orientation == FR_PROGRESS_HORIZONTAL) {
            gfx_draw_rect(&gfx_ctx, (gfx_rect_t){(int32_t)scrollbar->bounds.x + offset,
                          (int32_t)scrollbar->bounds.y, (int32_t)scrollbar->thumb_size,
                          (int32_t)scrollbar->bounds.h}, scrollbar->thumb_color);
        } else {
            gfx_draw_rect(&gfx_ctx, (gfx_rect_t){(int32_t)scrollbar->bounds.x,
                          (int32_t)scrollbar->bounds.y + offset, (int32_t)scrollbar->bounds.w,
                          (int32_t)scrollbar->thumb_size}, scrollbar->thumb_color);
        }
    }
}

int fr_scrollbar_handle_event(fr_scrollbar_ex_t *scrollbar, fr_event_t *event)
{
    if (scrollbar == NULL || event == NULL) return 0;
    return 0;
}

/* ================================================================
 *  分组框
 * ================================================================ */

fr_groupbox_ex_t *fr_groupbox_create(fr_rect_t bounds, const char *title)
{
    fr_groupbox_ex_t *group = (fr_groupbox_ex_t *)fr_alloc(sizeof(fr_groupbox_ex_t));
    if (group == NULL) return NULL;

    memset(group, 0, sizeof(fr_groupbox_ex_t));
    group->bounds = bounds;
    if (title) {
        strncpy(group->title, title, 63);
    }
    group->border_color = 0x999999;
    group->title_color = 0x000000;
    group->bg_color = 0xF5F5F5;
    group->visible = 1;
    group->collapsible = 0;
    group->collapsed = 0;
    return group;
}

void fr_groupbox_destroy(fr_groupbox_ex_t *group)
{
    if (group) fr_free(group);
}

void fr_groupbox_render(fr_groupbox_ex_t *group, fr_context_t *ctx)
{
    if (group == NULL || ctx == NULL || !group->visible) return;

    gfx_context_t gfx_ctx;
    fr_ctx_to_gfx(ctx, &gfx_ctx);

    /* 边框 */
    gfx_draw_rect(&gfx_ctx, (gfx_rect_t){(int32_t)group->bounds.x, (int32_t)group->bounds.y,
                  (int32_t)group->bounds.w, (int32_t)group->bounds.h}, group->border_color);
    gfx_draw_rect(&gfx_ctx, (gfx_rect_t){(int32_t)group->bounds.x + 1, (int32_t)group->bounds.y + 12,
                  (int32_t)group->bounds.w - 2, (int32_t)group->bounds.h - 13}, group->bg_color);
}

/* ================================================================
 *  菜单
 * ================================================================ */

fr_menu_ex_t *fr_menu_create(void)
{
    fr_menu_ex_t *menu = (fr_menu_ex_t *)fr_alloc(sizeof(fr_menu_ex_t));
    if (menu == NULL) return NULL;

    memset(menu, 0, sizeof(fr_menu_ex_t));
    menu->item_height = 24;
    menu->bg_color = 0xFFFFFF;
    menu->hover_color = 0xCCE0FF;
    menu->text_color = 0x000000;
    menu->disabled_color = 0x999999;
    menu->separator_color = 0xCCCCCC;
    return menu;
}

void fr_menu_destroy(fr_menu_ex_t *menu)
{
    if (menu) fr_free(menu);
}

int fr_menu_add_item(fr_menu_ex_t *menu, const char *label, uint32_t id,
                     void (*callback)(uint32_t id))
{
    if (menu == NULL || menu->item_count >= FR_MENU_MAX_ITEMS || label == NULL) return -1;

    fr_menu_item_ex_t *item = &menu->items[menu->item_count];
    memset(item, 0, sizeof(fr_menu_item_ex_t));
    strncpy(item->label, label, FR_MENU_MAX_LABEL - 1);
    item->id = id;
    item->enabled = 1;
    item->on_click = callback;
    menu->item_count++;
    return 0;
}

int fr_menu_add_separator(fr_menu_ex_t *menu)
{
    if (menu == NULL || menu->item_count >= FR_MENU_MAX_ITEMS) return -1;

    fr_menu_item_ex_t *item = &menu->items[menu->item_count];
    memset(item, 0, sizeof(fr_menu_item_ex_t));
    item->separator = 1;
    menu->item_count++;
    return 0;
}

int fr_menu_add_submenu(fr_menu_ex_t *menu, const char *label, fr_menu_ex_t *submenu)
{
    if (menu == NULL || menu->item_count >= FR_MENU_MAX_ITEMS || label == NULL || submenu == NULL) return -1;

    fr_menu_item_ex_t *item = &menu->items[menu->item_count];
    memset(item, 0, sizeof(fr_menu_item_ex_t));
    strncpy(item->label, label, FR_MENU_MAX_LABEL - 1);
    item->submenu = (struct fr_menu_item_ex *)submenu;
    menu->item_count++;
    return 0;
}

int fr_menu_add_item_ex(fr_menu_ex_t *menu, const char *label, uint32_t id,
                        uint32_t key, uint32_t mods, void (*callback)(uint32_t id))
{
    int ret = fr_menu_add_item(menu, label, id, callback);
    if (ret == 0) {
        menu->items[menu->item_count - 1].shortcut_key = key;
        menu->items[menu->item_count - 1].shortcut_mods = mods;
    }
    return ret;
}

void fr_menu_show(fr_menu_ex_t *menu, int x, int y)
{
    if (menu == NULL) return;
    menu->bounds.x = x;
    menu->bounds.y = y;
    menu->bounds.w = 200;
    menu->bounds.h = menu->item_count * menu->item_height;
    menu->is_open = 1;
    menu->visible = 1;
}

void fr_menu_hide(fr_menu_ex_t *menu)
{
    if (menu == NULL) return;
    menu->is_open = 0;
    menu->visible = 0;
}

void fr_menu_render(fr_menu_ex_t *menu, fr_context_t *ctx)
{
    if (menu == NULL || ctx == NULL || !menu->visible) return;

    gfx_context_t gfx_ctx;
    fr_ctx_to_gfx(ctx, &gfx_ctx);

    /* 菜单背景 */
    gfx_draw_rect(&gfx_ctx, (gfx_rect_t){(int32_t)menu->bounds.x, (int32_t)menu->bounds.y,
                  (int32_t)menu->bounds.w, (int32_t)menu->bounds.h}, menu->bg_color);

    /* 绘制菜单项 */
    for (uint32_t i = 0; i < menu->item_count; i++) {
        fr_menu_item_ex_t *item = &menu->items[i];
        int32_t y = (int32_t)(menu->bounds.y + i * menu->item_height);

        if (item->separator) {
            /* 分隔线 */
            gfx_draw_line(&gfx_ctx, (int32_t)menu->bounds.x + 8, y + (int32_t)(menu->item_height / 2),
                          (int32_t)(menu->bounds.x + menu->bounds.w) - 8, y + (int32_t)(menu->item_height / 2),
                          menu->separator_color);
        } else {
            /* 高亮背景 */
            if (i == menu->hover_index && item->enabled) {
                gfx_draw_rect(&gfx_ctx, (gfx_rect_t){(int32_t)menu->bounds.x + 1, y,
                              (int32_t)menu->bounds.w - 2, (int32_t)menu->item_height}, menu->hover_color);
            }
        }
    }
}

int fr_menu_handle_event(fr_menu_ex_t *menu, fr_event_t *event)
{
    if (menu == NULL || event == NULL) return 0;
    return 0;
}

void fr_menu_set_enabled(fr_menu_ex_t *menu, uint32_t index, int enabled)
{
    if (menu == NULL || index >= menu->item_count) return;
    menu->items[index].enabled = (uint8_t)enabled;
}

void fr_menu_set_checked(fr_menu_ex_t *menu, uint32_t index, int checked)
{
    if (menu == NULL || index >= menu->item_count) return;
    menu->items[index].checked = (uint8_t)checked;
}