#ifndef FUNSOS_JSON_H
#define FUNSOS_JSON_H

/*
 * FUNSOS 轻量级 JSON 解析 API
 * 提供 JSON 解析、查询、构建等功能。
 * 纯用户态实现，不依赖系统调用。
 */

#include "stdint.h"

/* JSON 值类型 */
typedef enum {
    FUNSOS_JSON_NULL = 0,
    FUNSOS_JSON_BOOL,
    FUNSOS_JSON_INT,
    FUNSOS_JSON_DOUBLE,
    FUNSOS_JSON_STRING,
    FUNSOS_JSON_ARRAY,
    FUNSOS_JSON_OBJECT
} funsos_json_type_t;

/* JSON 值结构 */
typedef struct funsos_json_value {
    funsos_json_type_t type;               /* 值类型 */
    union {
        int              boolean;          /* 布尔值 */
        int64_t          int_val;          /* 整数值 */
        double           double_val;       /* 浮点值 */
        char            *string;           /* 字符串值 */
        struct {
            struct funsos_json_value **items;  /* 数组成员 */
            uint32_t          size;           /* 数组大小 */
        } array;                              /* 数组值 */
        struct {
            char                    **keys;    /* 对象键 */
            struct funsos_json_value **values;  /* 对象值 */
            uint32_t                 size;      /* 对象大小 */
        } object;                             /* 对象值 */
    } value;
} funsos_json_value_t;

/* JSON 解析错误码 */
#define FUNSOS_JSON_OK              0
#define FUNSOS_JSON_ERR_INVAL      -1   /* 无效参数 */
#define FUNSOS_JSON_ERR_NOMEM      -2   /* 内存不足 */
#define FUNSOS_JSON_ERR_PARSE      -3   /* 解析错误 */
#define FUNSOS_JSON_ERR_TYPE       -4   /* 类型错误 */
#define FUNSOS_JSON_ERR_NOTFOUND   -5   /* 未找到 */

/* ---- 解析 API ---- */

/*
 * 解析 JSON 字符串
 * 参数: str - JSON 字符串; len - 字符串长度; err - 接收错误码 (可为NULL)
 * 返回: JSON 值对象, NULL 失败
 */
funsos_json_value_t *funsos_json_parse(const char *str, uint32_t len, int *err);

/*
 * 从文件解析 JSON
 * 参数: filepath - 文件路径; err - 接收错误码 (可为NULL)
 * 返回: JSON 值对象, NULL 失败
 */
funsos_json_value_t *funsos_json_parse_file(const char *filepath, int *err);

/* ---- 释放 API ---- */

/*
 * 释放 JSON 值对象及其所有子对象
 * 参数: value - JSON 值对象
 */
void funsos_json_free(funsos_json_value_t *value);

/* ---- 类型查询 API ---- */

/*
 * 获取 JSON 值类型
 * 参数: value - JSON 值对象
 * 返回: 值类型
 */
funsos_json_type_t funsos_json_get_type(const funsos_json_value_t *value);

/*
 * 检查是否为 null
 * 参数: value - JSON 值对象
 * 返回: 1 是, 0 否
 */
int funsos_json_is_null(const funsos_json_value_t *value);

/*
 * 检查是否为布尔值
 * 参数: value - JSON 值对象
 * 返回: 1 是, 0 否
 */
int funsos_json_is_bool(const funsos_json_value_t *value);

/*
 * 检查是否为整数
 * 参数: value - JSON 值对象
 * 返回: 1 是, 0 否
 */
int funsos_json_is_int(const funsos_json_value_t *value);

/*
 * 检查是否为浮点数
 * 参数: value - JSON 值对象
 * 返回: 1 是, 0 否
 */
int funsos_json_is_double(const funsos_json_value_t *value);

/*
 * 检查是否为字符串
 * 参数: value - JSON 值对象
 * 返回: 1 是, 0 否
 */
int funsos_json_is_string(const funsos_json_value_t *value);

/*
 * 检查是否为数组
 * 参数: value - JSON 值对象
 * 返回: 1 是, 0 否
 */
int funsos_json_is_array(const funsos_json_value_t *value);

/*
 * 检查是否为对象
 * 参数: value - JSON 值对象
 * 返回: 1 是, 0 否
 */
int funsos_json_is_object(const funsos_json_value_t *value);

/* ---- 值获取 API ---- */

/*
 * 获取布尔值
 * 参数: value - JSON 值对象
 * 返回: 布尔值 (非布尔返回 0)
 */
int funsos_json_get_bool(const funsos_json_value_t *value);

/*
 * 获取整数值
 * 参数: value - JSON 值对象
 * 返回: 整数值 (非整数返回 0)
 */
int64_t funsos_json_get_int(const funsos_json_value_t *value);

/*
 * 获取浮点数值
 * 参数: value - JSON 值对象
 * 返回: 浮点数值 (非数字返回 0.0)
 */
double funsos_json_get_double(const funsos_json_value_t *value);

/*
 * 获取字符串值
 * 参数: value - JSON 值对象
 * 返回: 字符串指针 (非字符串返回 NULL)
 */
const char *funsos_json_get_string(const funsos_json_value_t *value);

/* ---- 数组操作 API ---- */

/*
 * 获取数组大小
 * 参数: value - JSON 数组对象
 * 返回: 数组大小 (非数组返回 0)
 */
uint32_t funsos_json_array_size(const funsos_json_value_t *value);

/*
 * 获取数组元素
 * 参数: value - JSON 数组对象; index - 索引
 * 返回: 元素值对象, NULL 越界或非数组
 */
funsos_json_value_t *funsos_json_array_get(const funsos_json_value_t *value, uint32_t index);

/* ---- 对象操作 API ---- */

/*
 * 获取对象大小
 * 参数: value - JSON 对象
 * 返回: 对象大小 (非对象返回 0)
 */
uint32_t funsos_json_object_size(const funsos_json_value_t *value);

/*
 * 获取对象成员值
 * 参数: value - JSON 对象; key - 键名
 * 返回: 值对象, NULL 未找到或非对象
 */
funsos_json_value_t *funsos_json_object_get(const funsos_json_value_t *value, const char *key);

/*
 * 检查对象是否包含键
 * 参数: value - JSON 对象; key - 键名
 * 返回: 1 包含, 0 不包含
 */
int funsos_json_object_has(const funsos_json_value_t *value, const char *key);

/*
 * 获取对象的键
 * 参数: value - JSON 对象; index - 索引
 * 返回: 键字符串, NULL 越界
 */
const char *funsos_json_object_key(const funsos_json_value_t *value, uint32_t index);

/*
 * 获取对象的值
 * 参数: value - JSON 对象; index - 索引
 * 返回: 值对象, NULL 越界
 */
funsos_json_value_t *funsos_json_object_value(const funsos_json_value_t *value, uint32_t index);

/* ---- 构建 API ---- */

/*
 * 创建 null 值
 * 返回: JSON 值对象, NULL 失败
 */
funsos_json_value_t *funsos_json_create_null(void);

/*
 * 创建布尔值
 * 参数: boolean - 布尔值
 * 返回: JSON 值对象, NULL 失败
 */
funsos_json_value_t *funsos_json_create_bool(int boolean);

/*
 * 创建整数值
 * 参数: val - 整数值
 * 返回: JSON 值对象, NULL 失败
 */
funsos_json_value_t *funsos_json_create_int(int64_t val);

/*
 * 创建浮点数值
 * 参数: val - 浮点数值
 * 返回: JSON 值对象, NULL 失败
 */
funsos_json_value_t *funsos_json_create_double(double val);

/*
 * 创建字符串值
 * 参数: str - 字符串
 * 返回: JSON 值对象, NULL 失败
 */
funsos_json_value_t *funsos_json_create_string(const char *str);

/*
 * 创建空数组
 * 返回: JSON 值对象, NULL 失败
 */
funsos_json_value_t *funsos_json_create_array(void);

/*
 * 创建空对象
 * 返回: JSON 值对象, NULL 失败
 */
funsos_json_value_t *funsos_json_create_object(void);

/*
 * 向数组添加元素
 * 参数: array - 数组; value - 元素值
 * 返回: 0 成功, -1 失败
 */
int funsos_json_array_append(funsos_json_value_t *array, funsos_json_value_t *value);

/*
 * 向对象添加键值对
 * 参数: object - 对象; key - 键; value - 值
 * 返回: 0 成功, -1 失败
 */
int funsos_json_object_set(funsos_json_value_t *object, const char *key,
                           funsos_json_value_t *value);

/* ---- 序列化 API ---- */

/*
 * 序列化为 JSON 字符串
 * 参数: value - JSON 值对象; buf - 接收缓冲区; bufsize - 缓冲区大小
 *       pretty - 是否美化输出
 * 返回: 写入的字符数 (不含 \0), -1 失败
 */
int funsos_json_serialize(const funsos_json_value_t *value, char *buf,
                          uint32_t bufsize, int pretty);

#endif /* FUNSOS_JSON_H */
