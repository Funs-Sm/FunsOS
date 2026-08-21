/* json_demo.c - JSON解析示例
 * 演示 FUNSOS SDK v1.5.0 轻量级 JSON 解析功能。
 */

#include "funsos.h"
#include "funsos_json.h"

/* 示例 JSON 字符串 */
static const char *g_sample_json =
    "{"
    "  \"name\": \"FUNSOS\","
    "  \"version\": \"1.5.0\","
    "  \"features\": ["
    "    \"window\","
    "    \"graphics\","
    "    \"network\""
    "  ],"
    "  \"config\": {"
    "    \"width\": 800,"
    "    \"height\": 600,"
    "    \"fullscreen\": false"
    "  },"
    "  \"released\": true"
    "}";

static char g_output[40][80];
static int  g_line_count = 0;

static void add_line(const char *fmt, ...)
{
    if (g_line_count >= 40) return;
    /* 简单格式化 */
    int i = 0;
    while (fmt[i] && i < 79) {
        g_output[g_line_count][i] = fmt[i];
        i++;
    }
    g_output[g_line_count][i] = '\0';
    g_line_count++;
}

int main(void)
{
    funsos_window_t win = funsos_create_window(100, 80, 650, 550, "JSON解析示例 v1.5.0");
    funsos_fill_window(win, 0xFFFFFF);

    funsos_color_t black = {0x00, 0x00, 0x00, 0xFF};
    funsos_color_t blue  = {0x00, 0x00, 0xFF, 0xFF};
    funsos_color_t green = {0x00, 0x80, 0x00, 0xFF};
    funsos_color_t red   = {0xFF, 0x00, 0x00, 0xFF};
    funsos_color_t purple = {0x80, 0x00, 0x80, 0xFF};
    funsos_color_t orange = {0xFF, 0x80, 0x00, 0xFF};

    funsos_draw_text(win, 20, 20, "FUNSOS SDK v1.5.0 - JSON 解析示例", blue);
    funsos_draw_text(win, 20, 45, "轻量级 JSON 解析/构建/序列化 API", black);

    /* 解析 JSON */
    int err = 0;
    funsos_json_value_t *root = funsos_json_parse(g_sample_json, -1, &err);

    if (root == NULL) {
        add_line("[ERR] JSON 解析失败");
    } else {
        add_line("[OK] JSON 解析成功");

        /* 读取字符串 */
        if (funsos_json_object_has(root, "name")) {
            funsos_json_value_t *name = funsos_json_object_get(root, "name");
            add_line("name = %s", funsos_json_get_string(name));
        }

        if (funsos_json_object_has(root, "version")) {
            funsos_json_value_t *ver = funsos_json_object_get(root, "version");
            add_line("version = %s", funsos_json_get_string(ver));
        }

        /* 读取布尔值 */
        if (funsos_json_object_has(root, "released")) {
            funsos_json_value_t *rel = funsos_json_object_get(root, "released");
            add_line("released = %s", funsos_json_get_bool(rel) ? "true" : "false");
        }

        /* 读取数组 */
        funsos_json_value_t *features = funsos_json_object_get(root, "features");
        if (features && funsos_json_is_array(features)) {
            add_line("features (%d items):", funsos_json_array_size(features));
            for (uint32_t i = 0; i < funsos_json_array_size(features); i++) {
                funsos_json_value_t *item = funsos_json_array_get(features, i);
                add_line("  [%d] %s", i, funsos_json_get_string(item));
            }
        }

        /* 读取嵌套对象 */
        funsos_json_value_t *config = funsos_json_object_get(root, "config");
        if (config && funsos_json_is_object(config)) {
            add_line("config:");
            funsos_json_value_t *w = funsos_json_object_get(config, "width");
            add_line("  width = %d", (int)funsos_json_get_int(w));
            funsos_json_value_t *h = funsos_json_object_get(config, "height");
            add_line("  height = %d", (int)funsos_json_get_int(h));
            funsos_json_value_t *fs = funsos_json_object_get(config, "fullscreen");
            add_line("  fullscreen = %s", funsos_json_get_bool(fs) ? "true" : "false");
        }
    }

    /* 显示解析结果 */
    funsos_draw_text(win, 20, 80, "解析结果:", blue);
    for (int i = 0; i < g_line_count && i < 20; i++) {
        funsos_color_t c = black;
        if (g_output[i][1] == 'O' && g_output[i][2] == 'K') c = green;
        if (g_output[i][1] == 'E' && g_output[i][2] == 'R') c = red;
        funsos_draw_text(win, 40, 105 + i * 20, g_output[i], c);
    }

    /* 显示 JSON API 列表 */
    funsos_draw_text(win, 350, 80, "JSON API 列表:", blue);
    funsos_draw_text(win, 370, 105, "解析:", purple);
    funsos_draw_text(win, 390, 125, "json_parse()       - 解析字符串", black);
    funsos_draw_text(win, 390, 145, "json_parse_file()  - 解析文件", black);
    funsos_draw_text(win, 390, 165, "json_free()        - 释放对象", black);

    funsos_draw_text(win, 370, 195, "查询:", purple);
    funsos_draw_text(win, 390, 215, "json_get_type()    - 获取类型", black);
    funsos_draw_text(win, 390, 235, "json_is_*()        - 类型判断", black);
    funsos_draw_text(win, 390, 255, "json_get_*()       - 获取值", black);
    funsos_draw_text(win, 390, 275, "json_array_get()   - 数组元素", black);
    funsos_draw_text(win, 390, 295, "json_object_get()  - 对象成员", black);

    funsos_draw_text(win, 370, 325, "构建:", purple);
    funsos_draw_text(win, 390, 345, "json_create_*()    - 创建值", black);
    funsos_draw_text(win, 390, 365, "json_array_append()- 追加元素", black);
    funsos_draw_text(win, 390, 385, "json_object_set()  - 设置成员", black);

    funsos_draw_text(win, 370, 415, "序列化:", purple);
    funsos_draw_text(win, 390, 435, "json_serialize()   - 转字符串", black);

    /* 显示 JSON 数据类型 */
    funsos_draw_text(win, 20, 470, "支持的数据类型:", blue);
    funsos_draw_text(win, 40, 495, "null  bool  int  double  string  array  object", orange);

    if (root) {
        funsos_json_free(root);
    }

    /* 事件循环 */
    funsos_event_t event;
    while (1) {
        if (funsos_wait_event(&event) != 0)
            continue;
        if (event.type == FUNSOS_EVENT_KEY_PRESS && event.key == 0x1B)
            break;
    }

    funsos_destroy_window(win);
    return 0;
}
