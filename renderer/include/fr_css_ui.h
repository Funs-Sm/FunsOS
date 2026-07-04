#ifndef FR_CSS_UI_H
#define FR_CSS_UI_H

#include "stdint.h"
#include "funrender.h"

/* ============================================================
 *  CSS UI Engine - CSS/HTML style UI for FunRender
 *  完全增量更新，不影响原有FunRender C API
 * ============================================================ */

/* ---- 常量定义 ---- */
#define CSS_MAX_SELECTORS     64
#define CSS_MAX_PROPS         64
#define CSS_MAX_RULES         128
#define CSS_MAX_NODES         256
#define CSS_MAX_CHILDREN      32
#define CSS_NAME_LEN          64
#define CSS_VALUE_LEN         256
#define CSS_TEXT_LEN          512

/* ---- CSS 属性枚举 ---- */
typedef enum {
    CSS_PROP_NONE = 0,
    CSS_PROP_WIDTH,
    CSS_PROP_HEIGHT,
    CSS_PROP_BACKGROUND_COLOR,
    CSS_PROP_COLOR,
    CSS_PROP_FONT_SIZE,
    CSS_PROP_FONT_WEIGHT,
    CSS_PROP_BORDER,
    CSS_PROP_BORDER_RADIUS,
    CSS_PROP_PADDING,
    CSS_PROP_PADDING_TOP,
    CSS_PROP_PADDING_BOTTOM,
    CSS_PROP_PADDING_LEFT,
    CSS_PROP_PADDING_RIGHT,
    CSS_PROP_MARGIN,
    CSS_PROP_MARGIN_TOP,
    CSS_PROP_MARGIN_BOTTOM,
    CSS_PROP_MARGIN_LEFT,
    CSS_PROP_MARGIN_RIGHT,
    CSS_PROP_DISPLAY,
    CSS_PROP_FLEX_DIRECTION,
    CSS_PROP_JUSTIFY_CONTENT,
    CSS_PROP_ALIGN_ITEMS,
    CSS_PROP_GAP,
    CSS_PROP_TEXT_ALIGN,
    CSS_PROP_OPACITY,
    CSS_PROP_BOX_SHADOW,
    CSS_PROP_TEXT_SHADOW,
    CSS_PROP_TRANSFORM,
    CSS_PROP_POSITION,
    CSS_PROP_TOP,
    CSS_PROP_BOTTOM,
    CSS_PROP_LEFT,
    CSS_PROP_RIGHT,
    CSS_PROP_Z_INDEX,
    CSS_PROP_OVERFLOW,
    CSS_PROP_CURSOR,
    CSS_PROP__COUNT
} css_prop_type_t;

/* ---- CSS 选择器类型 ---- */
typedef enum {
    CSS_SEL_TAG,      /* tag name (div, button, etc.) */
    CSS_SEL_CLASS,    /* .classname */
    CSS_SEL_ID,       /* #idname */
    CSS_SEL_UNIVERSAL /* * */
} css_selector_type_t;

/* ---- CSS 属性键值对 ---- */
typedef struct {
    css_prop_type_t type;
    char value[CSS_VALUE_LEN];
} css_property_t;

/* ---- CSS 规则 (选择器 + 属性列表) ---- */
typedef struct {
    css_selector_type_t sel_type;
    char selector[CSS_NAME_LEN];
    css_property_t props[CSS_MAX_PROPS];
    int prop_count;
} css_rule_t;

/* ---- CSS 样式表 ---- */
typedef struct {
    css_rule_t rules[CSS_MAX_RULES];
    int rule_count;
} css_stylesheet_t;

/* ---- DOM 节点类型 ---- */
typedef enum {
    DOM_DIV = 0,
    DOM_BUTTON,
    DOM_LABEL,
    DOM_INPUT,
    DOM_TEXTBOX,
    DOM_CHECKBOX,
    DOM_SLIDER,
    DOM_PROGRESS,
    DOM_IMAGE,
    DOM_PANEL,
    DOM_WINDOW,
    DOM_TEXT,
    DOM_SPAN,
    DOM_SECTION,
    DOM_HEADER,
    DOM_FOOTER,
    DOM__COUNT
} dom_node_type_t;

/* ---- DOM 节点样式（计算后）---- */
typedef struct {
    int width;
    int height;
    fr_color_t bg_color;
    fr_color_t fg_color;
    int font_size;
    int font_bold;
    int border_width;
    fr_color_t border_color;
    int border_radius;
    int padding_top, padding_bottom, padding_left, padding_right;
    int margin_top, margin_bottom, margin_left, margin_right;
    int display;      /* 0=none, 1=block, 2=flex, 3=inline */
    int flex_dir;     /* 0=row, 1=column */
    int justify;      /* 0=start, 1=center, 2=end, 3=space-between, 4=space-around */
    int align_items;  /* 0=start, 1=center, 2=end, 3=stretch */
    int gap;
    int text_align;   /* 0=left, 1=center, 2=right */
    float opacity;
    int has_shadow;
} dom_style_t;

/* ---- DOM 节点 ---- */
typedef struct dom_node {
    dom_node_type_t type;
    char tag[CSS_NAME_LEN];
    char id[CSS_NAME_LEN];
    char class_name[CSS_NAME_LEN];
    char text[CSS_TEXT_LEN];

    dom_style_t style;       /* 计算后的样式 */
    fr_rect_t layout;        /* 计算后的布局位置 */

    struct dom_node *parent;
    struct dom_node *children[CSS_MAX_CHILDREN];
    int child_count;

    fr_handle_t fr_widget;   /* 关联的FunRender控件（如果有） */
    int visible;
} dom_node_t;

/* ---- UI 文档（DOM 树 + 样式表）---- */
typedef struct {
    css_stylesheet_t stylesheet;
    dom_node_t *root;
    dom_node_t nodes[CSS_MAX_NODES];
    int node_count;
    fr_handle_t fr_ctx;
} ui_document_t;

/* ============================================================
 *  CSS Parser API
 * ============================================================ */

int css_parse_stylesheet(const char *css_text, css_stylesheet_t *sheet);
int css_parse_color(const char *str, fr_color_t *out_color);
const char *css_prop_name(css_prop_type_t type);

/* ============================================================
 *  HTML/XML Parser API
 * ============================================================ */

dom_node_t *ui_parse_html(ui_document_t *doc, const char *html_text);
dom_node_t *ui_create_node(ui_document_t *doc, dom_node_type_t type,
                           const char *tag, const char *id, const char *class_name);
int ui_add_child(dom_node_t *parent, dom_node_t *child);

/* ============================================================
 *  Style Engine API
 * ============================================================ */

void ui_apply_styles(ui_document_t *doc);
void ui_compute_style(ui_document_t *doc, dom_node_t *node);

/* ============================================================
 *  Layout Engine API
 * ============================================================ */

void ui_layout(ui_document_t *doc, int viewport_w, int viewport_h);

/* ============================================================
 *  Renderer API (FunRender bridge)
 * ============================================================ */

int ui_init(ui_document_t *doc, fr_handle_t fr_ctx);
void ui_render(ui_document_t *doc);
void ui_create_widgets(ui_document_t *doc);

/* ============================================================
 *  High-level API
 * ============================================================ */

int ui_load(ui_document_t *doc, fr_handle_t fr_ctx,
            const char *html_text, const char *css_text);
void ui_destroy(ui_document_t *doc);

#endif /* FR_CSS_UI_H */
