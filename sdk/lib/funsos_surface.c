/* funsos_surface.c - FUNSOS SDK Surface / Texture / Mesh / Font 管理 API
 * 基于 funsos_graphics.h 中的声明，提供用户态实现。
 * 包含: 表面(Surface)像素缓冲区管理、纹理(Texture)句柄管理、
 *       网格(Mesh)内置几何体生成、字体(Font)简化加载。
 */

#include "funsos.h"
#include "funsos_libc.h"
#include "stddef.h"

/* ================================================================
 *  Surface: 像素缓冲区管理
 * ================================================================ */

funsos_surface_t *funsos_create_surface(uint32_t w, uint32_t h, funsos_color_t fmt)
{
    funsos_surface_t *surf;
    uint32_t pixel_size;
    uint32_t total_bytes;

    if (w == 0 || h == 0)
        return NULL;

    /* 根据格式确定每像素字节数 */
    switch (fmt) {
        case FUNSOS_PIXEL_ARGB8888:
            pixel_size = 4;
            break;
        case FUNSOS_PIXEL_RGB565:
            pixel_size = 2;
            break;
        case FUNSOS_PIXEL_A8:
            pixel_size = 1;
            break;
        default:
            pixel_size = 4;  /* 默认 ARGB8888 */
            break;
    }

    total_bytes = w * h * pixel_size;

    /* 分配 surface 结构体 */
    surf = (funsos_surface_t *)funsos_alloc(sizeof(funsos_surface_t));
    if (surf == NULL)
        return NULL;

    /* 分配像素缓冲区 */
    surf->pixels = (uint32_t *)funsos_alloc(total_bytes);
    if (surf->pixels == NULL) {
        funsos_free(surf);
        return NULL;
    }

    /* 初始化结构体 */
    surf->width  = w;
    surf->height = h;
    surf->pitch  = w * pixel_size;
    surf->format = fmt;

    /* 清零像素缓冲区（透明黑色） */
    funsos_memset(surf->pixels, 0, total_bytes);

    return surf;
}

int funsos_destroy_surface(funsos_surface_t *surf)
{
    if (surf == NULL)
        return -1;

    if (surf->pixels != NULL)
        funsos_free(surf->pixels);

    funsos_free(surf);
    return 0;
}

funsos_color_t funsos_surface_get_pixel(funsos_surface_t *surf, int x, int y)
{
    if (surf == NULL || surf->pixels == NULL)
        return 0;

    /* 边界检查 */
    if (x < 0 || x >= (int)surf->width || y < 0 || y >= (int)surf->height)
        return 0;

    return surf->pixels[y * surf->width + x];
}

void funsos_surface_set_pixel(funsos_surface_t *surf, int x, int y, funsos_color_t color)
{
    if (surf == NULL || surf->pixels == NULL)
        return;

    /* 边界检查 */
    if (x < 0 || x >= (int)surf->width || y < 0 || y >= (int)surf->height)
        return;

    surf->pixels[y * surf->width + x] = color;
}

int funsos_blit_surface(uint32_t win_handle, funsos_surface_t *surf, int dx, int dy)
{
    funsos_gfx_context_t win_ctx;
    uint32_t sw, sh;
    uint32_t sx, sy;
    int tx, ty;

    if (surf == NULL || surf->pixels == NULL)
        return -1;

    /* 获取窗口图形上下文 */
    {
        void *ctx_ptr = funsos_get_window_context(win_handle);
        if (ctx_ptr == NULL)
            return -1;
        funsos_memcpy(&win_ctx, ctx_ptr, sizeof(funsos_gfx_context_t));
    }

    sw = surf->width;
    sh = surf->height;

    /* 将表面内容拷贝到窗口帧缓冲区 */
    for (sy = 0; sy < sh; sy++) {
        for (sx = 0; sx < sw; sx++) {
            tx = dx + (int)sx;
            ty = dy + (int)sy;

            /* 边界检查：目标在窗口范围内 */
            if (tx >= 0 && tx < (int)win_ctx.width &&
                ty >= 0 && ty < (int)win_ctx.height) {

                /* 裁剪区域检查 */
                if (tx >= win_ctx.clip.x &&
                    tx < win_ctx.clip.x + win_ctx.clip.w &&
                    ty >= win_ctx.clip.y &&
                    ty < win_ctx.clip.y + win_ctx.clip.h) {

                    win_ctx.buffer[ty * win_ctx.width + tx] =
                        surf->pixels[sy * sw + sx];
                }
            }
        }
    }

    return 0;
}

/* ================================================================
 *  Texture: 纹理句柄管理 (用户态纹理表)
 * ================================================================ */

#define MAX_TEXTURES 64

static struct {
    funsos_surface_t *surface;
    int               in_use;
    int               min_filter;
    int               mag_filter;
    int               wrap_s;
    int               wrap_t;
} tex_table[MAX_TEXTURES];

static void tex_table_init(void)
{
    static int initialized = 0;
    uint32_t i;

    if (!initialized) {
        for (i = 0; i < MAX_TEXTURES; i++) {
            tex_table[i].surface    = NULL;
            tex_table[i].in_use     = 0;
            tex_table[i].min_filter = FUNSOS_TEX_FILTER_NEAREST;
            tex_table[i].mag_filter = FUNSOS_TEX_FILTER_NEAREST;
            tex_table[i].wrap_s     = FUNSOS_TEX_WRAP_CLAMP;
            tex_table[i].wrap_t     = FUNSOS_TEX_WRAP_CLAMP;
        }
        initialized = 1;
    }
}

funsos_texture_t funsos_create_texture(funsos_surface_t *surf)
{
    uint32_t i;

    if (surf == NULL)
        return 0;

    tex_table_init();

    /* 查找空闲槽位 */
    for (i = 0; i < MAX_TEXTURES; i++) {
        if (!tex_table[i].in_use) {
            tex_table[i].in_use   = 1;
            tex_table[i].surface  = surf;
            tex_table[i].min_filter = FUNSOS_TEX_FILTER_LINEAR;
            tex_table[i].mag_filter = FUNSOS_TEX_FILTER_LINEAR;
            return (funsos_texture_t)(i + 1);  /* 句柄从 1 开始，0 表示无效 */
        }
    }

    return 0;  /* 无空闲槽位 */
}

int funsos_destroy_texture(funsos_texture_t tex)
{
    uint32_t idx;

    if (tex == 0)
        return -1;

    idx = (uint32_t)tex - 1;
    if (idx >= MAX_TEXTURES)
        return -1;

    if (!tex_table[idx].in_use)
        return -1;

    tex_table[idx].in_use   = 0;
    tex_table[idx].surface  = NULL;
    tex_table[idx].min_filter = FUNSOS_TEX_FILTER_NEAREST;
    tex_table[idx].mag_filter = FUNSOS_TEX_FILTER_NEAREST;
    tex_table[idx].wrap_s     = FUNSOS_TEX_WRAP_CLAMP;
    tex_table[idx].wrap_t     = FUNSOS_TEX_WRAP_CLAMP;

    return 0;
}

int funsos_bind_texture(funsos_texture_t tex, uint32_t unit)
{
    uint32_t idx;

    (void)unit;  /* 用户态实现中暂不区分纹理单元 */

    if (tex == 0)
        return -1;

    idx = (uint32_t)tex - 1;
    if (idx >= MAX_TEXTURES || !tex_table[idx].in_use)
        return -1;

    /* 绑定操作: 在用户态仅记录当前活动纹理 */
    /* 实际渲染时通过内核 syscall 使用 */
    return 0;
}

int funsos_set_tex_filter(funsos_texture_t tex, int min_f, int mag_f)
{
    uint32_t idx;

    if (tex == 0)
        return -1;

    idx = (uint32_t)tex - 1;
    if (idx >= MAX_TEXTURES || !tex_table[idx].in_use)
        return -1;

    tex_table[idx].min_filter = min_f;
    tex_table[idx].mag_filter = mag_f;
    return 0;
}

int funsos_set_tex_wrap(funsos_texture_t tex, int wrap_s, int wrap_t)
{
    uint32_t idx;

    if (tex == 0)
        return -1;

    idx = (uint32_t)tex - 1;
    if (idx >= MAX_TEXTURES || !tex_table[idx].in_use)
        return -1;

    tex_table[idx].wrap_s = wrap_s;
    tex_table[idx].wrap_t = wrap_t;
    return 0;
}

/* ================================================================
 *  Mesh: 内置几何体生成
 * ================================================================ */

/*
 * 辅助函数: 归一化向量
 */
static void vec3_normalize(funsos_vec3_t *v)
{
    float len;
    len = v->x * v->x + v->y * v->y + v->z * v->z;
    if (len > 0.00001f) {
        len = 1.0f / (float)funsos_sqrt((double)len);
        v->x *= len;
        v->y *= len;
        v->z *= len;
    }
}

/*
 * 辅助函数: 计算两个向量的叉积
 */
static funsos_vec3_t vec3_cross(funsos_vec3_t a, funsos_vec3_t b)
{
    funsos_vec3_t result;
    result.x = a.y * b.z - a.z * b.y;
    result.y = a.z * b.x - a.x * b.z;
    result.z = a.x * b.y - a.y * b.x;
    return result;
}

funsos_mesh_t *funsos_create_mesh(uint32_t type)
{
    funsos_mesh_t *mesh;
    int i, j;

    mesh = (funsos_mesh_t *)funsos_alloc(sizeof(funsos_mesh_t));
    if (mesh == NULL)
        return NULL;

    funsos_memset(mesh, 0, sizeof(funsos_mesh_t));

    switch (type) {
    /*
     * 类型 0: 单位立方体 (6面 × 2三角形/面 × 3顶点 = 36 索引,
     *         8 个唯一顶点)
     */
    case FUNSOS_MESH_CUBE: {
        static const funsos_vec3_t cube_positions[8] = {
            {-0.5f, -0.5f, -0.5f}, { 0.5f, -0.5f, -0.5f},
            { 0.5f,  0.5f, -0.5f}, {-0.5f,  0.5f, -0.5f},
            {-0.5f, -0.5f,  0.5f}, { 0.5f, -0.5f,  0.5f},
            { 0.5f,  0.5f,  0.5f}, {-0.5f,  0.5f,  0.5f}
        };
        static const uint16_t cube_indices[36] = {
            0,1,2, 0,2,3,   /* 前面 (-Z) */
            5,4,7, 5,7,6,   /* 后面 (+Z) */
            4,0,3, 4,3,7,   /* 左面 (-X) */
            1,5,6, 1,6,2,   /* 右面 (+X) */
            3,2,6, 3,6,7,   /* 上面 (+Y) */
            4,5,1, 4,1,0    /* 下面 (-Y) */
        };
        static const funsos_vec3_t cube_normals[6] = {
            { 0.0f,  0.0f, -1.0f}, { 0.0f,  0.0f,  1.0f},
            {-1.0f,  0.0f,  0.0f}, { 1.0f,  0.0f,  0.0f},
            { 0.0f,  1.0f,  0.0f}, { 0.0f, -1.0f,  0.0f}
        };

        mesh->vertex_count = 8;
        mesh->index_count  = 36;

        mesh->positions = (funsos_vec3_t *)funsos_alloc(
            sizeof(funsos_vec3_t) * mesh->vertex_count);
        mesh->normals   = (funsos_vec3_t *)funsos_alloc(
            sizeof(funsos_vec3_t) * mesh->vertex_count);
        mesh->texcoords = (float *)funsos_alloc(
            sizeof(float) * 2 * mesh->vertex_count);
        mesh->colors    = (funsos_rgba_t *)funsos_alloc(
            sizeof(funsos_rgba_t) * mesh->vertex_count);
        mesh->indices   = (uint16_t *)funsos_alloc(
            sizeof(uint16_t) * mesh->index_count);

        if (!mesh->positions || !mesh->normals || !mesh->texcoords ||
            !mesh->colors || !mesh->indices) {
            funsos_destroy_mesh(mesh);
            return NULL;
        }

        for (i = 0; i < 8; i++) {
            mesh->positions[i] = cube_positions[i];

            /* 每个顶点根据所在面设置法线 */
            {
                int face = i / 4;  /* 每个面4个顶点 */
                if (face >= 6) face = 5;
                mesh->normals[i] = cube_normals[face];
            }

            /* UV 坐标 */
            mesh->texcoords[i * 2]     = cube_positions[i].x + 0.5f;
            mesh->texcoords[i * 2 + 1] = cube_positions[i].y + 0.5f;

            /* 默认白色 */
            mesh->colors[i].r = 255;
            mesh->colors[i].g = 255;
            mesh->colors[i].b = 255;
            mesh->colors[i].a = 255;
        }

        for (i = 0; i < 36; i++)
            mesh->indices[i] = cube_indices[i];

        break;
    }

    /*
     * 类型 1: UV 球体 (细分级别 12, 12×24=288 顶点, ~1296 索引)
     */
    case FUNSOS_MESH_SPHERE: {
#define SPHERE_SEGMENTS 12
#define SPHERE_RINGS    12
        int segs = SPHERE_SEGMENTS;
        int rings = SPHERE_RINGS;
        int vert_count = (rings + 1) * (segs + 1);
        int idx_count = rings * segs * 6;
        int vi = 0;
        int ii = 0;

        mesh->vertex_count = (uint32_t)vert_count;
        mesh->index_count  = (uint32_t)idx_count;

        mesh->positions = (funsos_vec3_t *)funsos_alloc(
            sizeof(funsos_vec3_t) * mesh->vertex_count);
        mesh->normals   = (funsos_vec3_t *)funsos_alloc(
            sizeof(funsos_vec3_t) * mesh->vertex_count);
        mesh->texcoords = (float *)funsos_alloc(
            sizeof(float) * 2 * mesh->vertex_count);
        mesh->colors    = (funsos_rgba_t *)funsos_alloc(
            sizeof(funsos_rgba_t) * mesh->vertex_count);
        mesh->indices   = (uint16_t *)funsos_alloc(
            sizeof(uint16_t) * mesh->index_count);

        if (!mesh->positions || !mesh->normals || !mesh->texcoords ||
            !mesh->colors || !mesh->indices) {
            funsos_destroy_mesh(mesh);
            return NULL;
        }

        /* 生成顶点 */
        for (j = 0; j <= rings; j++) {
            float phi = 3.14159265f * (float)j / (float)rings;
            for (i = 0; i <= segs; i++) {
                float theta = 2.0f * 3.14159265f * (float)i / (float)segs;

                funsos_vec3_t pos;
                pos.x = (float)funsos_sinf(phi) * (float)funsos_cosf(theta);
                pos.y = (float)funsos_cosf(phi);
                pos.z = (float)funsos_sinf(phi) * (float)funsos_sinf(theta);

                mesh->positions[vi] = pos;

                /* 法线 = 归一化位置（单位球体） */
                mesh->normals[vi] = pos;
                vec3_normalize(&mesh->normals[vi]);

                /* UV 映射 */
                mesh->texcoords[vi * 2]     = (float)i / (float)segs;
                mesh->texcoords[vi * 2 + 1] = (float)j / (float)rings;

                /* 白色 */
                mesh->colors[vi].r = 255;
                mesh->colors[vi].g = 255;
                mesh->colors[vi].b = 255;
                mesh->colors[vi].a = 255;

                vi++;
            }
        }

        /* 生成索引（三角形列表） */
        for (j = 0; j < rings; j++) {
            for (i = 0; i < segs; i++) {
                int a = j * (segs + 1) + i;
                int b = a + segs + 1;
                int c = a + 1;
                int d = b + 1;

                mesh->indices[ii++] = (uint16_t)a;
                mesh->indices[ii++] = (uint16_t)b;
                mesh->indices[ii++] = (uint16_t)c;
                mesh->indices[ii++] = (uint16_t)b;
                mesh->indices[ii++] = (uint16_t)d;
                mesh->indices[ii++] = (uint16_t)c;
            }
        }
#undef SPHERE_SEGMENTS
#undef SPHERE_RINGS
        break;
    }

    /*
     * 类型 2: 平面网格 (2个三角形, 4顶点, 6索引)
     */
    case FUNSOS_MESH_PLANE: {
        static const funsos_vec3_t plane_positions[4] = {
            {-0.5f, 0.0f, -0.5f}, { 0.5f, 0.0f, -0.5f},
            { 0.5f, 0.0f,  0.5f}, {-0.5f, 0.0f,  0.5f}
        };
        static const uint16_t plane_indices[6] = {0, 1, 2, 0, 2, 3};
        static const funsos_vec3_t plane_normal = {0.0f, 1.0f, 0.0f};

        mesh->vertex_count = 4;
        mesh->index_count  = 6;

        mesh->positions = (funsos_vec3_t *)funsos_alloc(sizeof(funsos_vec3_t) * 4);
        mesh->normals   = (funsos_vec3_t *)funsos_alloc(sizeof(funsos_vec3_t) * 4);
        mesh->texcoords = (float *)funsos_alloc(sizeof(float) * 8);
        mesh->colors    = (funsos_rgba_t *)funsos_alloc(sizeof(funsos_rgba_t) * 4);
        mesh->indices   = (uint16_t *)funsos_alloc(sizeof(uint16_t) * 6);

        if (!mesh->positions || !mesh->normals || !mesh->texcoords ||
            !mesh->colors || !mesh->indices) {
            funsos_destroy_mesh(mesh);
            return NULL;
        }

        for (i = 0; i < 4; i++) {
            mesh->positions[i] = plane_positions[i];
            mesh->normals[i]   = plane_normal;
            mesh->colors[i].r  = 255;
            mesh->colors[i].g  = 255;
            mesh->colors[i].b  = 255;
            mesh->colors[i].a  = 255;
        }

        /* UV: [0,0]-[1,1] */
        mesh->texcoords[0] = 0.0f; mesh->texcoords[1] = 0.0f;
        mesh->texcoords[2] = 1.0f; mesh->texcoords[3] = 0.0f;
        mesh->texcoords[4] = 1.0f; mesh->texcoords[5] = 1.0f;
        mesh->texcoords[6] = 0.0f; mesh->texcoords[7] = 1.0f;

        for (i = 0; i < 6; i++)
            mesh->indices[i] = plane_indices[i];

        break;
    }

    /*
     * 类型 3: 圆柱体
     */
    case FUNSOS_MESH_CYLINDER: {
#define CYL_SEGMENTS 16
#define CYL_STACKS   1
        int c_segs = CYL_SEGMENTS;
        int c_stacks = CYL_STACKS;
        int cv_count = (c_stacks + 1) * (c_segs + 1) + 2 * (c_segs + 1); /* 侧面+顶盖+底盖 */
        int ci_count = c_stacks * c_segs * 6 + 2 * c_segs * 3; /* 侧面+盖子 */
        int cvi = 0;
        int cii = 0;

        mesh->vertex_count = (uint32_t)cv_count;
        mesh->index_count  = (uint32_t)ci_count;

        mesh->positions = (funsos_vec3_t *)funsos_alloc(
            sizeof(funsos_vec3_t) * mesh->vertex_count);
        mesh->normals   = (funsos_vec3_t *)funsos_alloc(
            sizeof(funsos_vec3_t) * mesh->vertex_count);
        mesh->texcoords = (float *)funsos_alloc(
            sizeof(float) * 2 * mesh->vertex_count);
        mesh->colors    = (funsos_rgba_t *)funsos_alloc(
            sizeof(funsos_rgba_t) * mesh->vertex_count);
        mesh->indices   = (uint16_t *)funsos_alloc(
            sizeof(uint16_t) * mesh->index_count);

        if (!mesh->positions || !mesh->normals || !mesh->texcoords ||
            !mesh->colors || !mesh->indices) {
            funsos_destroy_mesh(mesh);
            return NULL;
        }

        /* 侧面顶点 */
        for (j = 0; j <= c_stacks; j++) {
            float y = (float)j / (float)c_stacks - 0.5f;
            for (i = 0; i <= c_segs; i++) {
                float theta = 2.0f * 3.14159265f * (float)i / (float)c_segs;
                float ct = (float)funsos_cosf(theta);
                float st = (float)funsos_sinf(theta);

                mesh->positions[cvi].x = 0.5f * ct;
                mesh->positions[cvi].y = y;
                mesh->positions[cvi].z = 0.5f * st;

                /* 法线: 径向朝外 */
                mesh->normals[cvi].x = ct;
                mesh->normals[cvi].y = 0.0f;
                mesh->normals[cvi].z = st;

                mesh->texcoords[cvi * 2]     = (float)i / (float)c_segs;
                mesh->texcoords[cvi * 2 + 1] = (float)j / (float)c_stacks;

                mesh->colors[cvi].r = 255;
                mesh->colors[cvi].g = 255;
                mesh->colors[cvi].b = 255;
                mesh->colors[cvi].a = 255;
                cvi++;
            }
        }

        /* 侧面索引 */
        for (j = 0; j < c_stacks; j++) {
            for (i = 0; i < c_segs; i++) {
                int a = j * (c_segs + 1) + i;
                int b = a + c_segs + 1;
                int c = a + 1;
                int d = b + 1;
                mesh->indices[cii++] = (uint16_t)a;
                mesh->indices[cii++] = (uint16_t)b;
                mesh->indices[cii++] = (uint16_t)c;
                mesh->indices[cii++] = (uint16_t)b;
                mesh->indices[cii++] = (uint16_t)d;
                mesh->indices[cii++] = (uint16_t)c;
            }
        }

        /* 顶盖中心 */
        {
            mesh->positions[cvi].x = 0.0f;
            mesh->positions[cvi].y = 0.5f;
            mesh->positions[cvi].z = 0.0f;
            mesh->normals[cvi].x = 0.0f;
            mesh->normals[cvi].y = 1.0f;
            mesh->normals[cvi].z = 0.0f;
            mesh->texcoords[cvi * 2] = 0.5f;
            mesh->texcoords[cvi * 2 + 1] = 1.0f;
            mesh->colors[cvi].r = 255;
            mesh->colors[cvi].g = 255;
            mesh->colors[cvi].b = 255;
            mesh->colors[cvi].a = 255;
            cvi++;
        }

        /* 顶盖边缘 */
        for (i = 0; i <= c_segs; i++) {
            float theta = 2.0f * 3.14159265f * (float)i / (float)c_segs;
            mesh->positions[cvi].x = 0.5f * (float)funsos_cosf(theta);
            mesh->positions[cvi].y = 0.5f;
            mesh->positions[cvi].z = 0.5f * (float)funsos_sinf(theta);
            mesh->normals[cvi].x = 0.0f;
            mesh->normals[cvi].y = 1.0f;
            mesh->normals[cvi].z = 0.0f;
            mesh->texcoords[cvi * 2] = 0.5f + 0.5f * (float)funsos_cosf(theta);
            mesh->texcoords[cvi * 2 + 1] = 0.5f + 0.5f * (float)funsos_sinf(theta);
            mesh->colors[cvi].r = 255;
            mesh->colors[cvi].g = 255;
            mesh->colors[cvi].b = 255;
            mesh->colors[cvi].a = 255;
            cvi++;
        }

        /* 顶盖三角形 */
        {
            int top_center_idx = cvi - c_segs - 2;
            for (i = 0; i < c_segs; i++) {
                mesh->indices[cii++] = (uint16_t)top_center_idx;
                mesh->indices[cii++] = (uint16_t)(top_center_idx + 1 + i);
                mesh->indices[cii++] = (uint16_t)(top_center_idx + 2 + i);
            }
        }

        /* 底盖中心 */
        {
            mesh->positions[cvi].x = 0.0f;
            mesh->positions[cvi].y = -0.5f;
            mesh->positions[cvi].z = 0.0f;
            mesh->normals[cvi].x = 0.0f;
            mesh->normals[cvi].y = -1.0f;
            mesh->normals[cvi].z = 0.0f;
            mesh->texcoords[cvi * 2] = 0.5f;
            mesh->texcoords[cvi * 2 + 1] = 0.0f;
            mesh->colors[cvi].r = 255;
            mesh->colors[cvi].g = 255;
            mesh->colors[cvi].b = 255;
            mesh->colors[cvi].a = 255;
            cvi++;
        }

        /* 底盖边缘 */
        for (i = 0; i <= c_segs; i++) {
            float theta = 2.0f * 3.14159265f * (float)i / (float)c_segs;
            mesh->positions[cvi].x = 0.5f * (float)funsos_cosf(theta);
            mesh->positions[cvi].y = -0.5f;
            mesh->positions[cvi].z = 0.5f * (float)funsos_sinf(theta);
            mesh->normals[cvi].x = 0.0f;
            mesh->normals[cvi].y = -1.0f;
            mesh->normals[cvi].z = 0.0f;
            mesh->texcoords[cvi * 2] = 0.5f + 0.5f * (float)funsos_cosf(theta);
            mesh->texcoords[cvi * 2 + 1] = 0.5f + 0.5f * (float)funsos_sinf(theta);
            mesh->colors[cvi].r = 255;
            mesh->colors[cvi].g = 255;
            mesh->colors[cvi].b = 255;
            mesh->colors[cvi].a = 255;
            cvi++;
        }

        /* 底盖三角形 */
        {
            int bot_center_idx = cvi - c_segs - 2;
            for (i = 0; i < c_segs; i++) {
                mesh->indices[cii++] = (uint16_t)bot_center_idx;
                mesh->indices[cii++] = (uint16_t)(bot_center_idx + 2 + i);
                mesh->indices[cii++] = (uint16_t)(bot_center_idx + 1 + i);
            }
        }
#undef CYL_SEGMENTS
#undef CYL_STACKS
        break;
    }

    /*
     * 类型 4: 圆锥体
     */
    case FUNSOS_MESH_CONE: {
#define CONE_SEGMENTS 16
        int cone_segs = CONE_SEGMENTS;
        int cone_vert_count = cone_segs + 2;  /* 顶端 + 底部圆环 + 底心 */
        int cone_idx_count = cone_segs * 3 + cone_segs * 3;  /* 侧面 + 底盖 */
        int ccvi = 0;
        int ccii = 0;

        mesh->vertex_count = (uint32_t)cone_vert_count;
        mesh->index_count  = (uint32_t)cone_idx_count;

        mesh->positions = (funsos_vec3_t *)funsos_alloc(
            sizeof(funsos_vec3_t) * mesh->vertex_count);
        mesh->normals   = (funsos_vec3_t *)funsos_alloc(
            sizeof(funsos_vec3_t) * mesh->vertex_count);
        mesh->texcoords = (float *)funsos_alloc(
            sizeof(float) * 2 * mesh->vertex_count);
        mesh->colors    = (funsos_rgba_t *)funsos_alloc(
            sizeof(funsos_rgba_t) * mesh->vertex_count);
        mesh->indices   = (uint16_t *)funsos_alloc(
            sizeof(uint16_t) * mesh->index_count);

        if (!mesh->positions || !mesh->normals || !mesh->texcoords ||
            !mesh->colors || !mesh->indices) {
            funsos_destroy_mesh(mesh);
            return NULL;
        }

        /* 顶点 */
        mesh->positions[ccvi].x = 0.0f;
        mesh->positions[ccvi].y = 0.5f;
        mesh->positions[ccvi].z = 0.0f;
        mesh->normals[ccvi].x = 0.0f;
        mesh->normals[ccvi].y = 1.0f;
        mesh->normals[ccvi].z = 0.0f;
        mesh->texcoords[ccvi * 2] = 0.5f;
        mesh->texcoords[ccvi * 2 + 1] = 1.0f;
        mesh->colors[ccvi].r = 255;
        mesh->colors[ccvi].g = 255;
        mesh->colors[ccvi].b = 255;
        mesh->colors[ccvi].a = 255;
        ccvi++;

        /* 底部圆环 */
        for (i = 0; i <= cone_segs; i++) {
            float theta = 2.0f * 3.14159265f * (float)i / (float)cone_segs;
            float ct = (float)funsos_cosf(theta);
            float st = (float)funsos_sinf(theta);

            mesh->positions[ccvi].x = 0.5f * ct;
            mesh->positions[ccvi].y = -0.5f;
            mesh->positions[ccvi].z = 0.5f * st;

            /* 侧面法线: 从底边指向顶点的切平面法线 */
            {
                funsos_vec3_t edge = {0.0f - 0.5f * ct, 0.5f - (-0.5f), 0.0f - 0.5f * st};
                funsos_vec3_t radial = {ct, 0.0f, st};
                funsos_vec3_t n = vec3_cross(radial, edge);
                vec3_normalize(&n);
                mesh->normals[ccvi] = n;
            }

            mesh->texcoords[ccvi * 2]     = (float)i / (float)cone_segs;
            mesh->texcoords[ccvi * 2 + 1] = 0.0f;
            mesh->colors[ccvi].r = 255;
            mesh->colors[ccvi].g = 255;
            mesh->colors[ccvi].b = 255;
            mesh->colors[ccvi].a = 255;
            ccvi++;
        }

        /* 侧面三角形 */
        for (i = 0; i < cone_segs; i++) {
            mesh->indices[ccii++] = 0;                      /* 顶点 */
            mesh->indices[ccii++] = (uint16_t)(1 + i);      /* 底边起点 */
            mesh->indices[ccii++] = (uint16_t)(1 + i + 1);  /* 底边终点 */
        }

        /* 底盖中心 */
        mesh->positions[ccvi].x = 0.0f;
        mesh->positions[ccvi].y = -0.5f;
        mesh->positions[ccvi].z = 0.0f;
        mesh->normals[ccvi].x = 0.0f;
        mesh->normals[ccvi].y = -1.0f;
        mesh->normals[ccvi].z = 0.0f;
        mesh->texcoords[ccvi * 2] = 0.5f;
        mesh->texcoords[ccvi * 2 + 1] = 0.5f;
        mesh->colors[ccvi].r = 255;
        mesh->colors[ccvi].g = 255;
        mesh->colors[ccvi].b = 255;
        mesh->colors[ccvi].a = 255;
        ccvi++;

        /* 底盖三角形 */
        {
            int base_center = ccvi - 1;
            for (i = 0; i < cone_segs; i++) {
                mesh->indices[ccii++] = (uint16_t)base_center;
                mesh->indices[ccii++] = (uint16_t)(1 + i + 1);
                mesh->indices[ccii++] = (uint16_t)(1 + i);
            }
        }
#undef CONE_SEGMENTS
        break;
    }

    default:
        funsos_free(mesh);
        return NULL;
    }

    return mesh;
}

int funsos_destroy_mesh(funsos_mesh_t *mesh)
{
    if (mesh == NULL)
        return -1;

    if (mesh->positions != NULL)
        funsos_free(mesh->positions);
    if (mesh->normals != NULL)
        funsos_free(mesh->normals);
    if (mesh->texcoords != NULL)
        funsos_free(mesh->texcoords);
    if (mesh->colors != NULL)
        funsos_free(mesh->colors);
    if (mesh->indices != NULL)
        funsos_free(mesh->indices);

    funsos_free(mesh);
    return 0;
}

int funsos_render_mesh(const funsos_mesh_t *mesh, funsos_mat4_t mvp)
{
    funsos_vertex3d_t *vertices;
    uint32_t i;

    if (mesh == NULL || mesh->positions == NULL || mesh->index_count == 0)
        return -1;

    /* 构建带颜色的顶点数组用于渲染 */
    vertices = (funsos_vertex3d_t *)funsos_alloc(
        sizeof(funsos_vertex3d_t) * mesh->vertex_count);
    if (vertices == NULL)
        return -1;

    for (i = 0; i < mesh->vertex_count; i++) {
        vertices[i].pos   = mesh->positions[i];
        vertices[i].color = (mesh->colors != NULL) ? mesh->colors[i]
                          : ((funsos_rgba_t){255, 255, 255, 255});
    }

    /* 调用 3D 渲染管线 */
    funsos_3d_render(vertices, mesh->vertex_count, mvp, FUNSOS_RENDER_TRIANGLES);

    funsos_free(vertices);
    return 0;
}

/* ================================================================
 *  Font: 简化的字体加载 (使用系统字体)
 * ================================================================ */

funsos_font_t funsos_load_font(const char *path, uint32_t size)
{
    (void)path;
    (void)size;
    /* 返回一个非空句柄表示"使用系统默认字体" */
    return (funsos_font_t)1;
}

int funsos_unload_font(funsos_font_t font)
{
    (void)font;
    return 0;
}

int funsos_draw_text_ex(uint32_t win_handle, funsos_font_t font, int x, int y,
                        const char *text, funsos_color_t color, uint32_t style)
{
    (void)font;
    (void)style;
    /* 回退到系统文本绘制 */
    return funsos_draw_text(win_handle, x, y, text, color);
}

int funsos_measure_text(funsos_font_t font, const char *text,
                        uint32_t *w_out, uint32_t *h_out)
{
    (void)font;

    if (text == NULL)
        return -1;

    if (w_out != NULL)
        *w_out = funsos_strlen(text) * 8;   /* 假设字符宽度 8px */
    if (h_out != NULL)
        *h_out = 16;                         /* 假设行高 16px */

    return 0;
}

funsos_font_t funsos_set_default_font(funsos_font_t font)
{
    (void)font;
    return NULL;
}
