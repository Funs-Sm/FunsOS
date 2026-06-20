/* funsos_pipeline.c - FUNSOS SDK 渲染管线状态管理
 * 实现 funsos_graphics.h 中声明的管线配置函数:
 *   - funsos_get_pipeline()
 *   - funsos_set_pipeline()
 *   - funsos_reset_pipeline()
 */

#include "funsos.h"
#include "funsos_libc.h"
#include "stddef.h"

/* ================================================================
 *  渲染管线状态 (静态存储)
 * ================================================================ */

static funsos_pipeline_state_t g_pipeline;

/*
 * 管线状态初始化标志
 */
static int g_pipeline_initialized = 0;

/*
 * 初始化管线状态为默认值（惰性初始化）
 */
static void pipeline_ensure_init(void)
{
    if (!g_pipeline_initialized) {
        funsos_memset(&g_pipeline, 0, sizeof(g_pipeline));

        /* 默认值设置 */
        g_pipeline.cull_face    = 1;     /* 启用背面剔除 */
        g_pipeline.cull_mode    = 0;     /* 剔除背面 */
        g_pipeline.depth_test   = 1;     /* 启用深度测试 */
        g_pipeline.depth_write  = 1;     /* 启用深度写入 */
        g_pipeline.blend_enable = 0;     /* 禁用 Alpha 混合 */
        g_pipeline.blend_src    = FUNSOS_BLEND_SRC_ALPHA;
        g_pipeline.blend_dst    = FUNSOS_BLEND_ONE_MINUS_SRC_ALPHA;
        g_pipeline.clear_color  = FUNSOS_COLOR_BLACK;
        g_pipeline.fog_color    = FUNSOS_COLOR_GRAY;
        g_pipeline.fog_start    = 10.0f;
        g_pipeline.fog_end      = 100.0f;
        g_pipeline.fog_enable   = 0;     /* 禁用雾效果 */

        g_pipeline_initialized = 1;
    }
}

/* ================================================================
 *  公共 API 实现
 * ================================================================ */

/*
 * funsos_get_pipeline() - 获取当前渲染管线状态
 * 返回指向内部静态存储的指针，调用者不应释放。
 */
funsos_pipeline_state_t *funsos_get_pipeline(void)
{
    pipeline_ensure_init();
    return &g_pipeline;
}

/*
 * funsos_set_pipeline() - 应用渲染管线状态
 * 将传入的状态复制到内部管线状态，并通知内核更新。
 */
int funsos_set_pipeline(const funsos_pipeline_state_t *state)
{
    if (state == NULL)
        return -1;

    pipeline_ensure_init();

    /* 复制整个状态结构体 */
    funsos_memcpy(&g_pipeline, state, sizeof(funsos_pipeline_state_t));

    /*
     * 在实际实现中，这里会通过 syscall 将状态传递给内核渲染器。
     * 用户态实现仅保存状态，实际的 GPU/渲染器配置由内核完成。
     */

    return 0;
}

/*
 * funsos_reset_pipeline() - 重置渲染管线状态为默认值
 */
void funsos_reset_pipeline(void)
{
    pipeline_ensure_init();

    g_pipeline.cull_face    = 1;
    g_pipeline.cull_mode    = 0;
    g_pipeline.depth_test   = 1;
    g_pipeline.depth_write  = 1;
    g_pipeline.blend_enable = 0;
    g_pipeline.blend_src    = FUNSOS_BLEND_SRC_ALPHA;
    g_pipeline.blend_dst    = FUNSOS_BLEND_ONE_MINUS_SRC_ALPHA;
    g_pipeline.clear_color  = FUNSOS_COLOR_BLACK;
    g_pipeline.fog_color    = FUNSOS_COLOR_GRAY;
    g_pipeline.fog_start    = 10.0f;
    g_pipeline.fog_end      = 100.0f;
    g_pipeline.fog_enable   = 0;
}
