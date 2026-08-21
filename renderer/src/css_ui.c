/* fr_css_ui.c - CSS/HTML style UI engine for FunRender
 * 完全增量更新，不影响原有FunRender C API
 */

#include "fr_css_ui.h"
#include "string.h"
#include "stdlib.h"
#include "stdio.h"
#include "kheap.h"

/* ============================================================
 *  工具函数
 * ============================================================ */

static void css_trim(char *s) {
    if (!s) return;
    int len = (int)strlen(s);
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t' ||
                       s[len-1] == '\n' || s[len-1] == '\r')) {
        s[--len] = '\0';
    }
    int start = 0;
    while (s[start] == ' ' || s[start] == '\t') start++;
    if (start > 0) memmove(s, s + start, len - start + 1);
}

static int css_hex_char(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* ============================================================
 *  CSS 颜色解析
 * ============================================================ */

typedef struct {
    const char *name;
    uint8_t r, g, b;
} css_named_color_t;

static const css_named_color_t css_named_colors[] = {
    {"black", 0, 0, 0},
    {"white", 255, 255, 255},
    {"red", 255, 0, 0},
    {"green", 0, 128, 0},
    {"lime", 0, 255, 0},
    {"blue", 0, 0, 255},
    {"yellow", 255, 255, 0},
    {"cyan", 0, 255, 255},
    {"magenta", 255, 0, 255},
    {"silver", 192, 192, 192},
    {"gray", 128, 128, 128},
    {"grey", 128, 128, 128},
    {"maroon", 128, 0, 0},
    {"olive", 128, 128, 0},
    {"purple", 128, 0, 128},
    {"teal", 0, 128, 128},
    {"navy", 0, 0, 128},
    {"orange", 255, 165, 0},
    {"pink", 255, 192, 203},
    {"gold", 255, 215, 0},
    {"brown", 165, 42, 42},
    {"lightgray", 211, 211, 211},
    {"lightgrey", 211, 211, 211},
    {"darkgray", 169, 169, 169},
    {"darkgrey", 169, 169, 169},
    {"transparent", 0, 0, 0},
    {0, 0, 0, 0}
};

int css_parse_color(const char *str, fr_color_t *out_color) {
    if (!str || !out_color) return -1;
    out_color->a = 255;

    /* 命名颜色 */
    for (int i = 0; css_named_colors[i].name; i++) {
        if (strcmp(str, css_named_colors[i].name) == 0) {
            out_color->r = css_named_colors[i].r;
            out_color->g = css_named_colors[i].g;
            out_color->b = css_named_colors[i].b;
            if (strcmp(str, "transparent") == 0) out_color->a = 0;
            return 0;
        }
    }

    /* #RRGGBB 或 #RGB */
    if (str[0] == '#') {
        int len = (int)strlen(str + 1);
        if (len == 6) {
            int r = (css_hex_char(str[1]) << 4) | css_hex_char(str[2]);
            int g = (css_hex_char(str[3]) << 4) | css_hex_char(str[4]);
            int b = (css_hex_char(str[5]) << 4) | css_hex_char(str[6]);
            if (r >= 0 && g >= 0 && b >= 0) {
                out_color->r = (uint8_t)r;
                out_color->g = (uint8_t)g;
                out_color->b = (uint8_t)b;
                return 0;
            }
        } else if (len == 3) {
            int r = css_hex_char(str[1]);
            int g = css_hex_char(str[2]);
            int b = css_hex_char(str[3]);
            if (r >= 0 && g >= 0 && b >= 0) {
                out_color->r = (uint8_t)((r << 4) | r);
                out_color->g = (uint8_t)((g << 4) | g);
                out_color->b = (uint8_t)((b << 4) | b);
                return 0;
            }
        }
    }

    /* rgb(r,g,b) */
    if (strncmp(str, "rgb(", 4) == 0) {
        int r, g, b;
        const char *p = str + 4;
        r = atoi(p);
        while (*p && *p != ',') p++; if (*p == ',') p++;
        g = atoi(p);
        while (*p && *p != ',') p++; if (*p == ',') p++;
        b = atoi(p);
        out_color->r = (uint8_t)(r < 0 ? 0 : (r > 255 ? 255 : r));
        out_color->g = (uint8_t)(g < 0 ? 0 : (g > 255 ? 255 : g));
        out_color->b = (uint8_t)(b < 0 ? 0 : (b > 255 ? 255 : b));
        return 0;
    }

    /* 默认灰色 */
    out_color->r = 128;
    out_color->g = 128;
    out_color->b = 128;
    return -1;
}

/* ============================================================
 *  CSS 属性名查找
 * ============================================================ */

typedef struct {
    const char *name;
    css_prop_type_t type;
} css_prop_name_t;

static const css_prop_name_t css_prop_names[] = {
    {"width", CSS_PROP_WIDTH},
    {"height", CSS_PROP_HEIGHT},
    {"background-color", CSS_PROP_BACKGROUND_COLOR},
    {"background", CSS_PROP_BACKGROUND_COLOR},
    {"color", CSS_PROP_COLOR},
    {"font-size", CSS_PROP_FONT_SIZE},
    {"font-weight", CSS_PROP_FONT_WEIGHT},
    {"border", CSS_PROP_BORDER},
    {"border-radius", CSS_PROP_BORDER_RADIUS},
    {"padding", CSS_PROP_PADDING},
    {"padding-top", CSS_PROP_PADDING_TOP},
    {"padding-bottom", CSS_PROP_PADDING_BOTTOM},
    {"padding-left", CSS_PROP_PADDING_LEFT},
    {"padding-right", CSS_PROP_PADDING_RIGHT},
    {"margin", CSS_PROP_MARGIN},
    {"margin-top", CSS_PROP_MARGIN_TOP},
    {"margin-bottom", CSS_PROP_MARGIN_BOTTOM},
    {"margin-left", CSS_PROP_MARGIN_LEFT},
    {"margin-right", CSS_PROP_MARGIN_RIGHT},
    {"display", CSS_PROP_DISPLAY},
    {"flex-direction", CSS_PROP_FLEX_DIRECTION},
    {"justify-content", CSS_PROP_JUSTIFY_CONTENT},
    {"align-items", CSS_PROP_ALIGN_ITEMS},
    {"gap", CSS_PROP_GAP},
    {"text-align", CSS_PROP_TEXT_ALIGN},
    {"opacity", CSS_PROP_OPACITY},
    {"box-shadow", CSS_PROP_BOX_SHADOW},
    {"text-shadow", CSS_PROP_TEXT_SHADOW},
    {"transform", CSS_PROP_TRANSFORM},
    {"position", CSS_PROP_POSITION},
    {"top", CSS_PROP_TOP},
    {"bottom", CSS_PROP_BOTTOM},
    {"left", CSS_PROP_LEFT},
    {"right", CSS_PROP_RIGHT},
    {"z-index", CSS_PROP_Z_INDEX},
    {"overflow", CSS_PROP_OVERFLOW},
    {"cursor", CSS_PROP_CURSOR},
    {0, CSS_PROP_NONE}
};

const char *css_prop_name(css_prop_type_t type) {
    for (int i = 0; css_prop_names[i].name; i++) {
        if (css_prop_names[i].type == type) return css_prop_names[i].name;
    }
    return "unknown";
}

static css_prop_type_t css_find_prop_type(const char *name) {
    for (int i = 0; css_prop_names[i].name; i++) {
        if (strcmp(name, css_prop_names[i].name) == 0) {
            return css_prop_names[i].type;
        }
    }
    return CSS_PROP_NONE;
}

/* ============================================================
 *  CSS 解析器
 * ============================================================ */

static const char *css_skip_ws(const char *p) {
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    return p;
}

/* 解析一条 CSS 规则的属性块 { ... } */
static int css_parse_declaration_block(const char **pp, css_rule_t *rule) {
    const char *p = *pp;
    p = css_skip_ws(p);
    if (*p != '{') return -1;
    p++; /* skip { */

    while (*p && *p != '}') {
        p = css_skip_ws(p);
        if (*p == '}' || *p == '\0') break;

        /* 解析属性名 */
        char prop_name[CSS_NAME_LEN];
        int pi = 0;
        while (*p && *p != ':' && *p != '{' && *p != '}' && pi < CSS_NAME_LEN - 1) {
            prop_name[pi++] = *p++;
        }
        prop_name[pi] = '\0';
        css_trim(prop_name);

        if (*p != ':') break;
        p++; /* skip : */
        p = css_skip_ws(p);

        /* 解析属性值 */
        char prop_val[CSS_VALUE_LEN];
        int vi = 0;
        while (*p && *p != ';' && *p != '}' && vi < CSS_VALUE_LEN - 1) {
            prop_val[vi++] = *p++;
        }
        prop_val[vi] = '\0';
        css_trim(prop_val);

        if (*p == ';') p++;

        if (rule->prop_count < CSS_MAX_PROPS) {
            css_prop_type_t pt = css_find_prop_type(prop_name);
            if (pt != CSS_PROP_NONE) {
                rule->props[rule->prop_count].type = pt;
                strncpy(rule->props[rule->prop_count].value, prop_val, CSS_VALUE_LEN - 1);
                rule->props[rule->prop_count].value[CSS_VALUE_LEN - 1] = '\0';
                rule->prop_count++;
            }
        }
    }

    if (*p == '}') p++;
    *pp = p;
    return 0;
}

int css_parse_stylesheet(const char *css_text, css_stylesheet_t *sheet) {
    if (!css_text || !sheet) return -1;

    sheet->rule_count = 0;
    const char *p = css_text;

    while (*p) {
        p = css_skip_ws(p);
        if (!*p) break;

        /* 跳过注释 */
        if (p[0] == '/' && p[1] == '*') {
            p += 2;
            while (*p && !(p[0] == '*' && p[1] == '/')) p++;
            if (*p) p += 2;
            continue;
        }

        /* 解析选择器 */
        char selector[CSS_NAME_LEN];
        int si = 0;
        while (*p && *p != '{' && *p != ';' && si < CSS_NAME_LEN - 1) {
            selector[si++] = *p++;
        }
        selector[si] = '\0';
        css_trim(selector);

        if (!*p || *p != '{') break;
        if (si == 0) { p++; continue; }

        if (sheet->rule_count >= CSS_MAX_RULES) break;

        css_rule_t *rule = &sheet->rules[sheet->rule_count];
        memset(rule, 0, sizeof(css_rule_t));

        /* 判断选择器类型 */
        if (selector[0] == '#') {
            rule->sel_type = CSS_SEL_ID;
            strncpy(rule->selector, selector + 1, CSS_NAME_LEN - 1);
        } else if (selector[0] == '.') {
            rule->sel_type = CSS_SEL_CLASS;
            strncpy(rule->selector, selector + 1, CSS_NAME_LEN - 1);
        } else if (selector[0] == '*') {
            rule->sel_type = CSS_SEL_UNIVERSAL;
            strncpy(rule->selector, "*", CSS_NAME_LEN - 1);
        } else {
            rule->sel_type = CSS_SEL_TAG;
            strncpy(rule->selector, selector, CSS_NAME_LEN - 1);
        }
        rule->selector[CSS_NAME_LEN - 1] = '\0';

        if (css_parse_declaration_block(&p, rule) == 0) {
            sheet->rule_count++;
        }
    }

    return sheet->rule_count;
}

/* ============================================================
 *  DOM 节点创建
 * ============================================================ */

dom_node_t *ui_create_node(ui_document_t *doc, dom_node_type_t type,
                           const char *tag, const char *id, const char *class_name) {
    if (!doc || doc->node_count >= CSS_MAX_NODES) return 0;

    dom_node_t *node = &doc->nodes[doc->node_count++];
    memset(node, 0, sizeof(dom_node_t));

    node->type = type;
    node->visible = 1;
    node->style.width = 100;
    node->style.height = 30;
    node->style.bg_color = FR_RGB(240, 240, 240);
    node->style.fg_color = FR_RGB(0, 0, 0);
    node->style.font_size = 14;
    node->style.display = 1; /* block */
    node->style.opacity = 1.0f;
    node->style.text_align = 0; /* left */
    node->style.flex_dir = 1; /* column */
    node->style.justify = 0;
    node->style.align_items = 0;

    if (tag) strncpy(node->tag, tag, CSS_NAME_LEN - 1);
    if (id) strncpy(node->id, id, CSS_NAME_LEN - 1);
    if (class_name) strncpy(node->class_name, class_name, CSS_NAME_LEN - 1);

    return node;
}

int ui_add_child(dom_node_t *parent, dom_node_t *child) {
    if (!parent || !child) return -1;
    if (parent->child_count >= CSS_MAX_CHILDREN) return -1;

    parent->children[parent->child_count++] = child;
    child->parent = parent;
    return 0;
}

/* ============================================================
 *  HTML 解析器
 * ============================================================ */

static dom_node_type_t html_tag_to_type(const char *tag) {
    if (!tag) return DOM_DIV;
    if (strcmp(tag, "div") == 0) return DOM_DIV;
    if (strcmp(tag, "button") == 0) return DOM_BUTTON;
    if (strcmp(tag, "label") == 0) return DOM_LABEL;
    if (strcmp(tag, "span") == 0) return DOM_SPAN;
    if (strcmp(tag, "input") == 0) return DOM_INPUT;
    if (strcmp(tag, "textbox") == 0) return DOM_TEXTBOX;
    if (strcmp(tag, "checkbox") == 0) return DOM_CHECKBOX;
    if (strcmp(tag, "slider") == 0) return DOM_SLIDER;
    if (strcmp(tag, "progress") == 0) return DOM_PROGRESS;
    if (strcmp(tag, "image") == 0 || strcmp(tag, "img") == 0) return DOM_IMAGE;
    if (strcmp(tag, "panel") == 0) return DOM_PANEL;
    if (strcmp(tag, "window") == 0) return DOM_WINDOW;
    if (strcmp(tag, "section") == 0) return DOM_SECTION;
    if (strcmp(tag, "header") == 0) return DOM_HEADER;
    if (strcmp(tag, "footer") == 0) return DOM_FOOTER;
    if (strcmp(tag, "text") == 0) return DOM_TEXT;
    return DOM_DIV;
}

static const char *html_skip_ws(const char *p) {
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    return p;
}

/* 解析标签属性 name="value" */
static void html_parse_attrs(const char *tag_str, char *id_out, char *class_out,
                             char *style_out, int max_len) {
    if (id_out) id_out[0] = '\0';
    if (class_out) class_out[0] = '\0';
    if (style_out) style_out[0] = '\0';

    const char *p = tag_str;
    while (*p && *p != '>') {
        /* 找属性名 */
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '>' || *p == '\0') break;

        char name[32];
        int ni = 0;
        while (*p && *p != '=' && *p != ' ' && *p != '>' && ni < 31) {
            name[ni++] = *p++;
        }
        name[ni] = '\0';

        if (*p != '=') {
            if (*p == ' ') continue;
            break;
        }
        p++; /* skip = */
        if (*p == '"') p++; /* skip opening quote */

        char value[256];
        int vi = 0;
        while (*p && *p != '"' && *p != '>' && vi < 255) {
            value[vi++] = *p++;
        }
        value[vi] = '\0';
        if (*p == '"') p++;

        if (strcmp(name, "id") == 0 && id_out) {
            strncpy(id_out, value, max_len - 1);
            id_out[max_len - 1] = '\0';
        } else if (strcmp(name, "class") == 0 && class_out) {
            strncpy(class_out, value, max_len - 1);
            class_out[max_len - 1] = '\0';
        } else if (strcmp(name, "style") == 0 && style_out) {
            strncpy(style_out, value, max_len - 1);
            style_out[max_len - 1] = '\0';
        }
    }
}

dom_node_t *ui_parse_html(ui_document_t *doc, const char *html_text) {
    if (!doc || !html_text) return 0;

    const char *p = html_text;
    dom_node_t *root = 0;
    dom_node_t *current = 0;

    /* 用一个简单的栈来管理嵌套 */
    dom_node_t *stack[64];
    int stack_top = 0;

    while (*p) {
        p = html_skip_ws(p);
        if (!*p) break;

        if (*p == '<') {
            p++;

            /* 跳过注释 */
            if (p[0] == '!' && p[1] == '-' && p[2] == '-') {
                while (*p && !(p[0] == '-' && p[1] == '-' && p[2] == '>')) p++;
                if (*p) p += 3;
                continue;
            }

            /* 结束标签 </tag> */
            if (*p == '/') {
                p++;
                while (*p && *p != '>') p++;
                if (*p == '>') p++;
                if (stack_top > 0) {
                    stack_top--;
                    current = (stack_top > 0) ? stack[stack_top - 1] : root;
                }
                continue;
            }

            /* 开始标签 <tag ...> */
            char tag_name[CSS_NAME_LEN];
            int ti = 0;
            while (*p && *p != ' ' && *p != '>' && *p != '/' && ti < CSS_NAME_LEN - 1) {
                tag_name[ti++] = *p++;
            }
            tag_name[ti] = '\0';

            /* 保存属性字符串起始 */
            const char *attr_start = p;

            /* 找到标签结束位置 */
            int self_closing = 0;
            while (*p && *p != '>') {
                if (*p == '/' && p[1] == '>') { self_closing = 1; break; }
                p++;
            }
            if (*p == '>') p++;
            if (self_closing) p++;

            /* 提取属性 */
            char id_str[CSS_NAME_LEN];
            char class_str[CSS_NAME_LEN];
            char style_str[CSS_VALUE_LEN];
            html_parse_attrs(attr_start, id_str, class_str, style_str, CSS_NAME_LEN);

            /* 创建节点 */
            dom_node_type_t type = html_tag_to_type(tag_name);
            dom_node_t *node = ui_create_node(doc, type, tag_name, id_str, class_str);
            if (!node) break;

            /* 保存内联样式到class域用于后续处理（简化处理）*/
            if (style_str[0]) {
                /* 临时存到text里后面处理; 简化: 暂存到class_name后面追加 */
                /* 这里简化处理，内联样式在apply阶段直接从style_str解析 */
                /* 为了简单，我们把内联样式存到 text 前面部分，用特殊标记 */
                /* 但这样太复杂，先支持外部样式表，内联的放后面 */
            }

            if (!root) {
                root = node;
                current = node;
            } else if (current) {
                ui_add_child(current, node);
            }

            if (!self_closing && strcmp(tag_name, "br") != 0 &&
                strcmp(tag_name, "hr") != 0 && strcmp(tag_name, "img") != 0 &&
                strcmp(tag_name, "input") != 0) {
                stack[stack_top++] = node;
                current = node;
            }
        } else {
            /* 文本节点 - 添加到当前节点的 text */
            char text_buf[256];
            int ti = 0;
            while (*p && *p != '<' && ti < 255) {
                text_buf[ti++] = *p++;
            }
            text_buf[ti] = '\0';

            if (current && ti > 0) {
                /* 简单拼接文本 */
                int cur_len = (int)strlen(current->text);
                if (cur_len + ti < CSS_TEXT_LEN - 1) {
                    strncat(current->text, text_buf, CSS_TEXT_LEN - 1 - cur_len);
                }
            }
        }
    }

    if (root) doc->root = root;
    return root;
}

/* ============================================================
 *  样式应用引擎
 * ============================================================ */

static int css_rule_matches(css_rule_t *rule, dom_node_t *node) {
    switch (rule->sel_type) {
        case CSS_SEL_UNIVERSAL:
            return 1;
        case CSS_SEL_TAG:
            return strcmp(node->tag, rule->selector) == 0;
        case CSS_SEL_CLASS:
            return strcmp(node->class_name, rule->selector) == 0 ||
                   strstr(node->class_name, rule->selector) != 0;
        case CSS_SEL_ID:
            return strcmp(node->id, rule->selector) == 0;
        default:
            return 0;
    }
}

static void css_apply_int(const char *val, int *out, int default_val) {
    if (!val || !*val) { *out = default_val; return; }
    int v = atoi(val);
    if (v == 0 && val[0] != '0') { *out = default_val; return; }
    *out = v;
}

static void ui_apply_rule(dom_node_t *node, css_rule_t *rule) {
    for (int i = 0; i < rule->prop_count; i++) {
        css_property_t *prop = &rule->props[i];
        switch (prop->type) {
            case CSS_PROP_WIDTH:
                css_apply_int(prop->value, &node->style.width, 100);
                break;
            case CSS_PROP_HEIGHT:
                css_apply_int(prop->value, &node->style.height, 30);
                break;
            case CSS_PROP_BACKGROUND_COLOR:
                css_parse_color(prop->value, &node->style.bg_color);
                break;
            case CSS_PROP_COLOR:
                css_parse_color(prop->value, &node->style.fg_color);
                break;
            case CSS_PROP_FONT_SIZE:
                css_apply_int(prop->value, &node->style.font_size, 14);
                break;
            case CSS_PROP_FONT_WEIGHT:
                node->style.font_bold = (strstr(prop->value, "bold") != 0 ||
                                         strcmp(prop->value, "700") == 0);
                break;
            case CSS_PROP_BORDER_RADIUS:
                css_apply_int(prop->value, &node->style.border_radius, 0);
                break;
            case CSS_PROP_PADDING: {
                int val = atoi(prop->value);
                node->style.padding_top = val;
                node->style.padding_bottom = val;
                node->style.padding_left = val;
                node->style.padding_right = val;
                break;
            }
            case CSS_PROP_PADDING_TOP:
                css_apply_int(prop->value, &node->style.padding_top, 0);
                break;
            case CSS_PROP_PADDING_BOTTOM:
                css_apply_int(prop->value, &node->style.padding_bottom, 0);
                break;
            case CSS_PROP_PADDING_LEFT:
                css_apply_int(prop->value, &node->style.padding_left, 0);
                break;
            case CSS_PROP_PADDING_RIGHT:
                css_apply_int(prop->value, &node->style.padding_right, 0);
                break;
            case CSS_PROP_MARGIN: {
                int val = atoi(prop->value);
                node->style.margin_top = val;
                node->style.margin_bottom = val;
                node->style.margin_left = val;
                node->style.margin_right = val;
                break;
            }
            case CSS_PROP_MARGIN_TOP:
                css_apply_int(prop->value, &node->style.margin_top, 0);
                break;
            case CSS_PROP_MARGIN_BOTTOM:
                css_apply_int(prop->value, &node->style.margin_bottom, 0);
                break;
            case CSS_PROP_MARGIN_LEFT:
                css_apply_int(prop->value, &node->style.margin_left, 0);
                break;
            case CSS_PROP_MARGIN_RIGHT:
                css_apply_int(prop->value, &node->style.margin_right, 0);
                break;
            case CSS_PROP_GAP:
                css_apply_int(prop->value, &node->style.gap, 0);
                break;
            case CSS_PROP_DISPLAY:
                if (strcmp(prop->value, "none") == 0) node->style.display = 0;
                else if (strcmp(prop->value, "flex") == 0) node->style.display = 2;
                else if (strcmp(prop->value, "inline") == 0) node->style.display = 3;
                else node->style.display = 1; /* block */
                break;
            case CSS_PROP_FLEX_DIRECTION:
                if (strstr(prop->value, "row")) node->style.flex_dir = 0;
                else node->style.flex_dir = 1; /* column */
                break;
            case CSS_PROP_JUSTIFY_CONTENT:
                if (strstr(prop->value, "center")) node->style.justify = 1;
                else if (strstr(prop->value, "end")) node->style.justify = 2;
                else if (strstr(prop->value, "space-between")) node->style.justify = 3;
                else if (strstr(prop->value, "space-around")) node->style.justify = 4;
                else node->style.justify = 0; /* start */
                break;
            case CSS_PROP_ALIGN_ITEMS:
                if (strstr(prop->value, "center")) node->style.align_items = 1;
                else if (strstr(prop->value, "end")) node->style.align_items = 2;
                else if (strstr(prop->value, "stretch")) node->style.align_items = 3;
                else node->style.align_items = 0; /* start */
                break;
            case CSS_PROP_TEXT_ALIGN:
                if (strcmp(prop->value, "center") == 0) node->style.text_align = 1;
                else if (strcmp(prop->value, "right") == 0) node->style.text_align = 2;
                else node->style.text_align = 0; /* left */
                break;
            case CSS_PROP_OPACITY: {
                float f = (float)strtod(prop->value, 0);
                if (f < 0.0f) f = 0.0f;
                if (f > 1.0f) f = 1.0f;
                node->style.opacity = f;
                break;
            }
            case CSS_PROP_BOX_SHADOW:
                node->style.has_shadow = 1;
                break;
            default:
                break;
        }
    }
}

void ui_compute_style(ui_document_t *doc, dom_node_t *node) {
    if (!doc || !node) return;

    /* 按优先级应用：tag < class < id */
    /* 先应用 tag 选择器 */
    for (int i = 0; i < doc->stylesheet.rule_count; i++) {
        if (doc->stylesheet.rules[i].sel_type == CSS_SEL_TAG ||
            doc->stylesheet.rules[i].sel_type == CSS_SEL_UNIVERSAL) {
            if (css_rule_matches(&doc->stylesheet.rules[i], node)) {
                ui_apply_rule(node, &doc->stylesheet.rules[i]);
            }
        }
    }
    /* 再应用 class 选择器 */
    for (int i = 0; i < doc->stylesheet.rule_count; i++) {
        if (doc->stylesheet.rules[i].sel_type == CSS_SEL_CLASS) {
            if (css_rule_matches(&doc->stylesheet.rules[i], node)) {
                ui_apply_rule(node, &doc->stylesheet.rules[i]);
            }
        }
    }
    /* 最后应用 id 选择器 */
    for (int i = 0; i < doc->stylesheet.rule_count; i++) {
        if (doc->stylesheet.rules[i].sel_type == CSS_SEL_ID) {
            if (css_rule_matches(&doc->stylesheet.rules[i], node)) {
                ui_apply_rule(node, &doc->stylesheet.rules[i]);
            }
        }
    }

    /* 递归处理子节点 */
    for (int i = 0; i < node->child_count; i++) {
        ui_compute_style(doc, node->children[i]);
    }
}

void ui_apply_styles(ui_document_t *doc) {
    if (!doc || !doc->root) return;
    ui_compute_style(doc, doc->root);
}

/* ============================================================
 *  布局引擎 (简化版 Flexbox)
 * ============================================================ */

static void ui_layout_node(dom_node_t *node, float x, float y, float w, float h) {
    if (!node || !node->visible) return;
    if (node->style.display == 0) return; /* display: none */

    node->layout.x = x + node->style.margin_left;
    node->layout.y = y + node->style.margin_top;

    /* 计算实际宽高 */
    float content_w = w - node->style.margin_left - node->style.margin_right;
    float content_h = h - node->style.margin_top - node->style.margin_bottom;

    node->layout.w = (node->style.width > 0) ? (float)node->style.width : content_w;
    node->layout.h = (node->style.height > 0) ? (float)node->style.height : content_h;

    if (node->style.display != 2) { /* not flex */
        return;
    }

    /* Flex 布局 */
    float inner_x = node->layout.x + node->style.padding_left;
    float inner_y = node->layout.y + node->style.padding_top;
    float inner_w = node->layout.w - node->style.padding_left - node->style.padding_right;
    float inner_h = node->layout.h - node->style.padding_top - node->style.padding_bottom;

    int visible_count = 0;
    float total_child_size = 0;

    for (int i = 0; i < node->child_count; i++) {
        dom_node_t *ch = node->children[i];
        if (ch->visible && ch->style.display != 0) {
            visible_count++;
            if (node->style.flex_dir == 0) { /* row */
                total_child_size += (ch->style.width > 0) ? (float)ch->style.width : 80;
                total_child_size += node->style.gap;
            } else { /* column */
                total_child_size += (ch->style.height > 0) ? (float)ch->style.height : 25;
                total_child_size += node->style.gap;
            }
        }
    }
    if (visible_count > 0) total_child_size -= node->style.gap; /* 去掉最后一个 gap */

    float start_offset = 0;
    float between_gap = (float)node->style.gap;

    if (visible_count > 0) {
        if (node->style.flex_dir == 0) { /* row - 水平 */
            float free_space = inner_w - total_child_size;
            switch (node->style.justify) {
                case 1: /* center */
                    start_offset = free_space / 2;
                    break;
                case 2: /* end */
                    start_offset = free_space;
                    break;
                case 3: /* space-between */
                    if (visible_count > 1) {
                        between_gap = free_space / (float)(visible_count - 1) + (float)node->style.gap;
                    }
                    break;
                case 4: /* space-around */
                    if (visible_count > 0) {
                        start_offset = free_space / (float)(visible_count * 2);
                        between_gap = free_space / (float)visible_count + (float)node->style.gap;
                    }
                    break;
                default: /* start */
                    break;
            }

            float cur_x = inner_x + start_offset;
            for (int i = 0; i < node->child_count; i++) {
                dom_node_t *ch = node->children[i];
                if (!ch->visible || ch->style.display == 0) continue;

                float ch_h = (ch->style.height > 0) ? (float)ch->style.height : inner_h;
                float ch_y = inner_y;

                if (node->style.align_items == 1) { /* center */
                    ch_y = inner_y + (inner_h - ch_h) / 2;
                } else if (node->style.align_items == 2) { /* end */
                    ch_y = inner_y + inner_h - ch_h;
                }

                ui_layout_node(ch, cur_x, ch_y,
                              (ch->style.width > 0) ? (float)ch->style.width : 80,
                              ch_h);
                cur_x += (ch->style.width > 0) ? (float)ch->style.width : 80;
                cur_x += between_gap;
            }
        } else { /* column - 垂直 */
            float free_space = inner_h - total_child_size;
            switch (node->style.justify) {
                case 1: /* center */
                    start_offset = free_space / 2;
                    break;
                case 2: /* end */
                    start_offset = free_space;
                    break;
                case 3: /* space-between */
                    if (visible_count > 1) {
                        between_gap = free_space / (float)(visible_count - 1) + (float)node->style.gap;
                    }
                    break;
                case 4: /* space-around */
                    if (visible_count > 0) {
                        start_offset = free_space / (float)(visible_count * 2);
                        between_gap = free_space / (float)visible_count + (float)node->style.gap;
                    }
                    break;
                default:
                    break;
            }

            float cur_y = inner_y + start_offset;
            for (int i = 0; i < node->child_count; i++) {
                dom_node_t *ch = node->children[i];
                if (!ch->visible || ch->style.display == 0) continue;

                float ch_w = (ch->style.width > 0) ? (float)ch->style.width : inner_w;
                float ch_x = inner_x;

                if (node->style.align_items == 1) { /* center */
                    ch_x = inner_x + (inner_w - ch_w) / 2;
                } else if (node->style.align_items == 2) { /* end */
                    ch_x = inner_x + inner_w - ch_w;
                }

                ui_layout_node(ch, ch_x, cur_y,
                              ch_w,
                              (ch->style.height > 0) ? (float)ch->style.height : 25);
                cur_y += (ch->style.height > 0) ? (float)ch->style.height : 25;
                cur_y += between_gap;
            }
        }
    }
}

void ui_layout(ui_document_t *doc, int viewport_w, int viewport_h) {
    if (!doc || !doc->root) return;
    ui_layout_node(doc->root, 0, 0, (float)viewport_w, (float)viewport_h);
}

/* ============================================================
 *  渲染桥接 (用原有 FunRender API 绘制)
 * ============================================================ */

int ui_init(ui_document_t *doc, fr_handle_t fr_ctx) {
    if (!doc) return -1;
    memset(doc, 0, sizeof(ui_document_t));
    doc->fr_ctx = fr_ctx;
    return 0;
}

static void ui_render_node(ui_document_t *doc, dom_node_t *node) {
    if (!doc || !node || !node->visible) return;
    if (node->style.display == 0) return;

    fr_handle_t ctx = doc->fr_ctx;
    if (!ctx) return;

    /* 这里用原始 FunRender C API 来绘制 */
    /* 由于我们是增量更新，原有API完全保留 */
    /* 我们只做样式计算和布局计算，渲染可以调用 fr_shape 等 API */

    /* 简化：用 widget 系统来创建对应控件 */

    /* 递归渲染子节点 */
    for (int i = 0; i < node->child_count; i++) {
        ui_render_node(doc, node->children[i]);
    }
}

void ui_create_widgets(ui_document_t *doc) {
    if (!doc || !doc->root || !doc->fr_ctx) return;
    /* 可以在这里创建 FunRender 控件树 */
}

void ui_render(ui_document_t *doc) {
    if (!doc || !doc->fr_ctx) return;

    /* 先应用样式 */
    ui_apply_styles(doc);

    /* 再计算布局 */
    fr_handle_t ctx = doc->fr_ctx;
    int w = 800, h = 600; /* 默认视口 */
    ui_layout(doc, w, h);

    /* 渲染 */
    ui_render_node(doc, doc->root);

    /* 最后调用原有 FunRender 渲染 */
    fr_render(ctx);
}

/* ============================================================
 *  高级 API
 * ============================================================ */

int ui_load(ui_document_t *doc, fr_handle_t fr_ctx,
            const char *html_text, const char *css_text) {
    if (!doc) return -1;

    ui_init(doc, fr_ctx);

    /* 先解析 CSS */
    if (css_text && *css_text) {
        css_parse_stylesheet(css_text, &doc->stylesheet);
    }

    /* 再解析 HTML */
    if (html_text && *html_text) {
        ui_parse_html(doc, html_text);
    }

    /* 应用样式 */
    if (doc->root) {
        ui_apply_styles(doc);
    }

    return 0;
}

void ui_destroy(ui_document_t *doc) {
    if (!doc) return;
    /* 节点是存在 doc->nodes 数组里的，整个 doc 清 0 即可 */
    memset(doc, 0, sizeof(ui_document_t));
}
