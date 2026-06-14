/* loading.c - FunsOS 加载动画实现
 * 深色渐变背景 + 中心旋转弧线加载指示器 + "FunsOS" 文字
 * + 进文字 + 粒子特效
 */

#include "loading.h"
#include "font.h"
#include "gfx.h"
#include "string.h"

/* ── 颜色定义 ── */
#define COLOR_BG_TOP     0xFF0F0F23   /* 顶部: 深蓝紫 */
#define COLOR_BG_BOTTOM  0xFF1A1A2E   /* 底部: 略亮蓝灰 */
#define COLOR_SPINNER_ON 0xFFFFFFFF   /* 活动弧段: 纯白 */
#define COLOR_SPINNER_OFF 0xFF3A3A5C  /* 非活动弧段: 暗灰紫 */
#define COLOR_TEXT        0xFFC8C8E0  /* 文字: 浅灰蓝 */
#define COLOR_PROGRESS    0xFF8888AA  /* 进度文字: 中灰紫 */
#define COLOR_PARTICLE    0xFF505078  /* 粒子: 半透明紫灰 */

/* ── 粒子结构 ── */
typedef struct {
    int x, y;
    int vy;       /* 垂直速度 (向上为负) */
    int alpha;    /* 透明度 (0-255, 简化为亮度乘数) */
} particle_t;

/* ── 全局状态 ── */
static int      g_screen_w = 0;
static int      g_screen_h = 0;
static uint32_t *g_fb = 0;
static uint32_t g_pitch = 0;
static int      g_frame = 0;
static int      g_done = 0;

static char     g_progress_msg[64] = "Starting FunsOS...";
static int      g_progress_pct = 0;
static int      g_progress_dots = 0;

static particle_t g_particles[LOADING_PARTICLE_COUNT];
static int      g_particles_inited = 0;

/* ── 简单伪随机 ── */
static uint32_t g_rand_seed = 12345;
static uint32_t lcg_rand(void) {
    g_rand_seed = g_rand_seed * 1103515245 + 12345;
    return g_rand_seed >> 16;
}

/* ── 预计算8方向 sin/cos 查找表 (定点: x256) ── */
/* 对应角度: 0°, 45°, 90°, 135°, 180°, 225°, 270°, 315° (屏幕坐标系, Y向下) */
static const int g_sin8[8] = {  0, 181, 256, 181,   0, -181, -256, -181 };
static const int g_cos8[8] = {256, 181,   0, -181, -256, -181,   0,  181 };

/* ── 辅助: 钳制值 ── */
static int clamp(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* ── 辅助: 混合两颜色(alpha混合) ── */
static uint32_t blend_color(uint32_t bg, uint32_t fg, int alpha) {
    int a2 = clamp(alpha, 0, 255);
    int a1 = 256 - a2;
    int rb = (((bg & 0x00FF00FF) * a1 + (fg & 0x00FF00FF) * a2) >> 8) & 0x00FF00FF;
    int g  = (((bg & 0x0000FF00) * a1 + (fg & 0x0000FF00) * a2) >> 8) & 0x0000FF00;
    return (uint32_t)(rb | g);
}

/* ── 辅助: 绘制像素到framebuffer ── */
static void draw_pixel(int x, int y, uint32_t color) {
    if (x >= 0 && x < g_screen_w && y >= 0 && y < g_screen_h) {
        g_fb[y * (g_pitch / 4) + x] = color;
    }
}

/* ── 辅助: 通过字体bitmap绘制字符 ── */
static void draw_char(int x, int y, char c, uint32_t fg, uint32_t bg) {
    if (c < 32 || c > 127) return;
    const uint8_t *glyph = font_data[(int)(c - 32)];
    for (int row = 0; row < FONT_GLYPH_HEIGHT; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < FONT_GLYPH_WIDTH; col++) {
            if (bits & (0x80 >> col)) {
                draw_pixel(x + col, y + row, fg);
            } else if (bg != 0xFFFFFFFF) {
                /* 如果 bg = 0xFFFFFFFF 则跳过背景绘制(透明) */
                draw_pixel(x + col, y + row, bg);
            }
        }
    }
}

/* ── 辅助: 绘制字符串 ── */
static void draw_string(int x, int y, const char *str, uint32_t fg, uint32_t bg) {
    while (*str) {
        draw_char(x, y, *str, fg, bg);
        x += FONT_GLYPH_WIDTH;
        str++;
    }
}

/* ── 辅助: 测量字符串像素宽度 ── */
static int measure_string(const char *str) {
    int len = 0;
    while (*str) { len += FONT_GLYPH_WIDTH; str++; }
    return len;
}

/* ── 绘制深色渐变背景 ── */
static void draw_gradient_bg(void) {
    for (int y = 0; y < g_screen_h; y++) {
        int alpha = (y * 256) / g_screen_h;  /* 0=top, 256=bottom */
        uint32_t color = blend_color(COLOR_BG_TOP, COLOR_BG_BOTTOM, alpha);
        for (int x = 0; x < g_screen_w; x++) {
            draw_pixel(x, y, color);
        }
    }
}

/* ── 判断像素属于哪个八分区段 (0-7, 顺时针从右侧开始) ── */
static int get_octant(int dx, int dy) {
    if (dx >= 0) {
        if (dy >= 0) {
            return (dx >= dy) ? 0 : 1;
        } else {
            int ndy = -dy;
            return (dx >= ndy) ? 7 : 6;
        }
    } else {
        int ndx = -dx;
        if (dy >= 0) {
            return (ndx >= dy) ? 3 : 2;
        } else {
            int ndy = -dy;
            return (ndx >= ndy) ? 4 : 5;
        }
    }
}

/* ── 绘制旋转弧线加载指示器 ── */
static void draw_spinner(int cx, int cy, int active_segment) {
    int R = LOADING_SPINNER_RADIUS;
    int half_w = LOADING_SPINNER_WIDTH / 2;
    int R_min = R - LOADING_SPINNER_WIDTH;

    for (int dy = -R; dy <= R; dy++) {
        for (int dx = -R; dx <= R; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 < R_min * R_min || d2 > R * R) continue;
            if (d2 <= 0) continue;

            int seg = get_octant(dx, dy);

            uint32_t color;
            if (seg == active_segment) {
                /* 活动弧段使用渐变高亮: 靠近两端稍暗, 中间最亮 */
                color = COLOR_SPINNER_ON;
            } else {
                color = COLOR_SPINNER_OFF;
            }

            draw_pixel(cx + dx, cy + dy, color);
        }
    }

    /* 在活动弧段端点绘制小圆点强化视觉效果 */
    for (int ep = 0; ep < 2; ep++) {
        int angle_idx = (ep == 0) ? active_segment : ((active_segment + 1) & 7);
        int ex = cx + (g_cos8[angle_idx] * R) / 256;
        int ey = cy + (g_sin8[angle_idx] * R) / 256;
        for (int dy = -2; dy <= 2; dy++) {
            for (int dx = -2; dx <= 2; dx++) {
                if (dx * dx + dy * dy <= 5) {
                    draw_pixel(ex + dx, ey + dy, COLOR_SPINNER_ON);
                }
            }
        }
    }
}

/* ── 初始化粒子效果 ── */
static void particles_init(void) {
    for (int i = 0; i < LOADING_PARTICLE_COUNT; i++) {
        g_particles[i].x = (int)(lcg_rand() % (uint32_t)g_screen_w);
        g_particles[i].y = (int)(lcg_rand() % (uint32_t)g_screen_h);
        g_particles[i].vy = -1 - (int)(lcg_rand() % 3); /* 向上速度 1-3 */
        g_particles[i].alpha = 60 + (int)(lcg_rand() % 100);
    }
    g_particles_inited = 1;
}

/* ── 更新并绘制粒子 ── */
static void particles_update_and_draw(void) {
    for (int i = 0; i < LOADING_PARTICLE_COUNT; i++) {
        /* 更新位置 */
        g_particles[i].y += g_particles[i].vy;

        /* 随机横向漂移 */
        g_particles[i].x += ((int)(lcg_rand() % 3)) - 1;

        /* 循环: 到达顶部后从底部重新出现 */
        if (g_particles[i].y < 0) {
            g_particles[i].y = g_screen_h + (int)(lcg_rand() % 20);
            g_particles[i].x = (int)(lcg_rand() % (uint32_t)g_screen_w);
            g_particles[i].vy = -1 - (int)(lcg_rand() % 3);
        }
        if (g_particles[i].y >= g_screen_h) {
            g_particles[i].y = 0;
            g_particles[i].x = (int)(lcg_rand() % (uint32_t)g_screen_w);
        }

        /* 绘制粒子 (小圆点, 2x2) */
        int alpha = g_particles[i].alpha;
        uint32_t particle_color = blend_color(COLOR_BG_BOTTOM, COLOR_PARTICLE, alpha);
        draw_pixel(g_particles[i].x, g_particles[i].y, particle_color);
        draw_pixel(g_particles[i].x + 1, g_particles[i].y, particle_color);
        draw_pixel(g_particles[i].x, g_particles[i].y + 1, particle_color);
        draw_pixel(g_particles[i].x + 1, g_particles[i].y + 1, particle_color);
    }
}

/* ── 绘制进度点 ── */
static void draw_progress_dots(int cx, int cy) {
    int dot_count = g_progress_dots % (LOADING_MAX_DOTS + 1);
    int start_x = cx - (LOADING_MAX_DOTS * LOADING_DOT_SPACING) / 2;

    for (int i = 0; i < LOADING_MAX_DOTS; i++) {
        uint32_t dot_color;
        if (i < dot_count) {
            dot_color = COLOR_TEXT;
        } else {
            dot_color = COLOR_SPINNER_OFF;
        }
        /* 绘制 3x3 方块作为点 */
        int dx = start_x + i * LOADING_DOT_SPACING + LOADING_DOT_SPACING / 2;
        for (int dy2 = -1; dy2 <= 1; dy2++) {
            for (int dx2 = -1; dx2 <= 1; dx2++) {
                draw_pixel(dx + dx2, cy + dy2, dot_color);
            }
        }
    }
}

/* ── 公共接口 ── */

void loading_screen_init(int w, int h, uint32_t *fb, uint32_t pitch) {
    g_screen_w = w;
    g_screen_h = h;
    g_fb = fb;
    g_pitch = pitch;
    g_frame = 0;
    g_done = 0;
    g_progress_pct = 0;
    g_progress_dots = 0;
    g_rand_seed = 12345;

    /* 初始化进度消息 */
    for (int i = 0; i < 63; i++) g_progress_msg[i] = 0;
    g_progress_msg[0] = 'S'; g_progress_msg[1] = 't'; g_progress_msg[2] = 'a';
    g_progress_msg[3] = 'r'; g_progress_msg[4] = 't'; g_progress_msg[5] = 'i';
    g_progress_msg[6] = 'n'; g_progress_msg[7] = 'g';
    g_progress_msg[8] = ' '; g_progress_msg[9] = 'F';
    g_progress_msg[10] = 'u'; g_progress_msg[11] = 'n';
    g_progress_msg[12] = 's'; g_progress_msg[13] = 'O';
    g_progress_msg[14] = 'S'; g_progress_msg[15] = '.';
    g_progress_msg[16] = '.'; g_progress_msg[17] = '.';

    particles_init();
}

void loading_screen_render_frame(void) {
    if (g_done) return;

    /* 1. 渐变背景 */
    draw_gradient_bg();

    /* 2. 粒子特效 */
    particles_update_and_draw();

    /* 3. 旋转弧线加载指示器 */
    int cx = g_screen_w / 2;
    int spinner_y = g_screen_h / 2 - 40;
    int active_segment = g_frame % LOADING_SPINNER_FRAMES;
    draw_spinner(cx, spinner_y, active_segment);

    /* 4. "FunsOS" 品牌文字 */
    const char *brand = "FunsOS";
    int brand_w = measure_string(brand);
    draw_string(cx - brand_w / 2, spinner_y + LOADING_SPINNER_RADIUS + 16,
                brand, COLOR_TEXT, 0xFFFFFFFF);

    /* 5. 进度文字 */
    if (g_progress_msg[0]) {
        int msg_w = measure_string(g_progress_msg);
        draw_string(cx - msg_w / 2, spinner_y + LOADING_SPINNER_RADIUS + 48,
                    g_progress_msg, COLOR_PROGRESS, 0xFFFFFFFF);
    }

    /* 6. 进度点 */
    draw_progress_dots(cx, spinner_y + LOADING_SPINNER_RADIUS + 70);

    /* 帧计数递增 */
    g_frame++;

    /* 每 6 帧更新一次进度点 (约每秒更新, 假设60fps则每100ms) */
    if (g_frame % 6 == 0 && !g_done) {
        g_progress_dots++;
    }
}

void loading_screen_set_progress(const char *msg, int pct) {
    if (msg) {
        int i;
        for (i = 0; i < 63 && msg[i]; i++) {
            g_progress_msg[i] = msg[i];
        }
        g_progress_msg[i] = '\0';
    }
    g_progress_pct = pct;
}

int loading_screen_is_done(void) {
    return g_done;
}

void loading_screen_mark_done(void) {
    g_done = 1;
}