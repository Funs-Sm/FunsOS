#ifndef FUNSOS_REGEX_H
#define FUNSOS_REGEX_H

/*
 * FUNSOS 正则表达式 API
 * 提供正则表达式编译、匹配、替换等功能。
 * 纯用户态实现，不依赖系统调用。
 */

#include "stdint.h"

/* 编译选项 */
#define FUNSOS_REG_ICASE     0x0001   /* 忽略大小写 */
#define FUNSOS_REG_NEWLINE   0x0002   /* 换行符特殊处理 */
#define FUNSOS_REG_NOTBOL    0x0004   /* 字符串起始不是行首 */
#define FUNSOS_REG_NOTEOL    0x0008   /* 字符串结尾不是行尾 */
#define FUNSOS_REG_DOTALL    0x0010   /* . 匹配换行符 */
#define FUNSOS_REG_EXTENDED  0x0020   /* 扩展正则表达式 */
#define FUNSOS_REG_NOSUB     0x0040   /* 不报告子表达式匹配 */

/* 执行选项 */
#define FUNSOS_REG_NOTBOL_EXEC  0x0001  /* 首字符不是行首 */
#define FUNSOS_REG_NOTEOL_EXEC  0x0002  /* 末字符不是行尾 */

/* 错误码 */
#define FUNSOS_REG_OK             0
#define FUNSOS_REG_NOMATCH       -1
#define FUNSOS_REG_BADPAT        -2
#define FUNSOS_REG_ECOLLATE      -3
#define FUNSOS_REG_ECTYPE        -4
#define FUNSOS_REG_EESCAPE       -5
#define FUNSOS_REG_ESUBREG       -6
#define FUNSOS_REG_EBRACK        -7
#define FUNSOS_REG_EPAREN        -8
#define FUNSOS_REG_EBRACE        -9
#define FUNSOS_REG_BADBR         -10
#define FUNSOS_REG_ERANGE        -11
#define FUNSOS_REG_ESPACE        -12
#define FUNSOS_REG_BADRPT        -13
#define FUNSOS_REG_EEND          -14
#define FUNSOS_REG_ESIZE         -15
#define FUNSOS_REG_EPAREN2       -16

/* 最大子表达式数量 */
#define FUNSOS_REG_MAX_SUB       32

/* 正则表达式不透明结构 */
typedef struct {
    void    *re_data;      /* 内部编译数据 */
    uint32_t re_nsub;      /* 捕获组数量 */
    int      re_flags;     /* 编译选项 */
} funsos_regex_t;

/* 匹配结果结构 */
typedef struct {
    int64_t rm_so;         /* 匹配起始偏移 */
    int64_t rm_eo;         /* 匹配结束偏移 */
} funsos_regmatch_t;

/* ---- 编译和释放 ---- */

/*
 * 编译正则表达式
 * 参数: preg - 正则结构指针; pattern - 正则表达式字符串; cflags - 编译选项
 * 返回: 0 成功, 错误码失败
 */
int funsos_regcomp(funsos_regex_t *preg, const char *pattern, int cflags);

/*
 * 释放正则表达式
 * 参数: preg - 正则结构指针
 */
void funsos_regfree(funsos_regex_t *preg);

/* ---- 匹配 ---- */

/*
 * 执行正则匹配
 * 参数: preg - 编译好的正则; string - 目标字符串
 *       nmatch - 匹配结果数组大小; pmatch - 匹配结果数组
 *       eflags - 执行选项
 * 返回: 0 成功匹配, REG_NOMATCH 不匹配, 其他错误
 */
int funsos_regexec(const funsos_regex_t *preg, const char *string,
                   uint32_t nmatch, funsos_regmatch_t *pmatch, int eflags);

/*
 * 获取错误描述
 * 参数: errcode - 错误码; errbuf - 接收缓冲区; errbuf_size - 缓冲区大小
 * 返回: 错误描述字符串长度
 */
uint32_t funsos_regerror(int errcode, const funsos_regex_t *preg,
                         char *errbuf, uint32_t errbuf_size);

/* ---- 高级功能 ---- */

/*
 * 字符串替换（替换第一个匹配）
 * 参数: preg - 编译好的正则; src - 源字符串
 *       replacement - 替换字符串 (支持 \1 等反向引用)
 *       dest - 目标缓冲区; destsize - 目标缓冲区大小
 * 返回: 替换后的字符串长度, -1 失败
 */
int funsos_regsub(const funsos_regex_t *preg, const char *src,
                  const char *replacement, char *dest, uint32_t destsize);

/*
 * 字符串替换（替换所有匹配）
 * 参数: preg - 编译好的正则; src - 源字符串
 *       replacement - 替换字符串
 *       dest - 目标缓冲区; destsize - 目标缓冲区大小
 * 返回: 替换次数, -1 失败
 */
int funsos_regsub_all(const funsos_regex_t *preg, const char *src,
                      const char *replacement, char *dest, uint32_t destsize);

/*
 * 分割字符串
 * 参数: preg - 编译好的正则; src - 源字符串
 *       parts - 接收分割结果的数组; maxparts - 最大分割数
 * 返回: 分割的段数, -1 失败
 */
int funsos_regsplit(const funsos_regex_t *preg, const char *src,
                    char **parts, uint32_t maxparts);

/*
 * 查找所有匹配
 * 参数: preg - 编译好的正则; src - 源字符串
 *       matches - 接收匹配结果的数组; maxmatches - 最大匹配数
 * 返回: 匹配次数, -1 失败
 */
int funsos_regfind_all(const funsos_regex_t *preg, const char *src,
                       funsos_regmatch_t *matches, uint32_t maxmatches);

#endif /* FUNSOS_REGEX_H */
