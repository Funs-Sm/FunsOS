#ifndef SHELL_ERROR_H
#define SHELL_ERROR_H

/*
 * shell_error.h - FUNSOS Shell 独立错误处理模块
 *
 * 为每个 shell 指令提供差异化的、上下文相关的错误提示。
 * 根据不同的输入方法（缺少参数、参数无效、文件未找到等）
 * 给出针对具体指令的建议和修复指引。
 */

#include "shell.h"

/* ---- 错误码定义 ---- */
#define SHELL_ERR_UNKNOWN_CMD    1
#define SHELL_ERR_FILE_NOT_FOUND 2
#define SHELL_ERR_DIR_NOT_FOUND  3
#define SHELL_ERR_PERM_DENIED    4
#define SHELL_ERR_INVALID_ARG    5
#define SHELL_ERR_MISSING_ARG    6
#define SHELL_ERR_TOO_MANY_ARGS  7
#define SHELL_ERR_NO_MEMORY      8
#define SHELL_ERR_READ_FAIL      9
#define SHELL_ERR_WRITE_FAIL     10
#define SHELL_ERR_CREATE_FAIL    11
#define SHELL_ERR_DELETE_FAIL    12
#define SHELL_ERR_RENAME_FAIL    13
#define SHELL_ERR_COPY_FAIL      14
#define SHELL_ERR_MOUNT_FAIL     15
#define SHELL_ERR_UMOUNT_FAIL    16
#define SHELL_ERR_FORMAT_FAIL    17
#define SHELL_ERR_DISK_FAIL      18
#define SHELL_ERR_NOT_DIR        19
#define SHELL_ERR_NOT_FILE       20
#define SHELL_ERR_IS_DIR         21
#define SHELL_ERR_FILE_EXISTS    22
#define SHELL_ERR_DIR_NOT_EMPTY  23
#define SHELL_ERR_NO_SPACE       24
#define SHELL_ERR_READ_ONLY      25
#define SHELL_ERR_NO_DEVICE      26
#define SHELL_ERR_DEVICE_BUSY    27
#define SHELL_ERR_NO_NETWORK     28
#define SHELL_ERR_CONN_FAIL      29
#define SHELL_ERR_TIMEOUT        30
#define SHELL_ERR_DNS_FAIL       31
#define SHELL_ERR_NO_PROCESS     32
#define SHELL_ERR_PROCESS_DEAD   33
#define SHELL_ERR_SIGNAL_FAIL    34
#define SHELL_ERR_SYSCALL_FAIL   35
#define SHELL_ERR_NOT_IMPL       36
#define SHELL_ERR_INTERNAL       37
#define SHELL_ERR_BAD_SYNTAX     38
#define SHELL_ERR_NO_ALIAS       39
#define SHELL_ERR_ALIAS_EXISTS   40
#define SHELL_ERR_LOOP           41
#define SHELL_ERR_NO_HELP        42
#define SHELL_ERR_ABORTED        43

/* ---- 通用错误接口 ---- */
void shell_error(int err_code, const char *context);

/* ---- 指令专属错误接口 ---- */
/* 每个函数根据指令名和错误类型，输出带有用法建议的详细错误信息 */

/* 文件操作指令错误 */
void shell_err_pt(void);            /* pt/ls 目录列表 */
void shell_err_show(const char *file); /* show/cat/type 查看文件 */
void shell_err_go(const char *dir);  /* go/cd 切换目录 */
void shell_err_copy(const char *src); /* copy 复制文件 */
void shell_err_del(const char *file); /* del 删除文件 */
void shell_err_mkdir(const char *dir); /* mkdir 创建目录 */
void shell_err_ren(void);           /* ren 重命名 */
void shell_err_find(void);          /* find 查找文件 */
void shell_err_size(const char *file); /* size 文件大小 */
void shell_err_echo(void);          /* echo 输出文本 */
void shell_err_set(void);           /* set 设置变量 */
void shell_err_run(const char *file); /* run 运行程序 */
void shell_err_cat(const char *file);  /* cat 查看文件 */
void shell_err_ls(void);            /* ls 列目录 */
void shell_err_cd(const char *dir); /* cd 切换目录 */
void shell_err_touch(const char *file); /* touch 创建文件 */
void shell_err_append(const char *file); /* append 追加写入 */
void shell_err_head(const char *file);  /* head 显示头部 */
void shell_err_tail(const char *file);  /* tail 显示尾部 */
void shell_err_wc(const char *file);    /* wc 统计 */
void shell_err_diff(void);          /* diff 比较 */
void shell_err_sort(const char *file);  /* sort 排序 */
void shell_err_uniq(const char *file);  /* uniq 去重 */
void shell_err_grep(void);          /* grep 搜索 */
void shell_err_replace(void);       /* replace 替换 */
void shell_err_chmod(void);         /* chmod 修改权限 */
void shell_err_chown(void);         /* chown 修改所有者 */
void shell_err_stat(const char *file);  /* stat 文件状态 */
void shell_err_tree(const char *dir);   /* tree 目录树 */
void shell_err_du(const char *dir);     /* du 磁盘用量 */
void shell_err_edit(const char *file);  /* edit 编辑器 */
void shell_err_wget(const char *url);     /* wget 下载文件 */
void shell_err_traceroute(const char *ip); /* traceroute 路由跟踪 */
void shell_err_base64(const char *file);  /* base64 编解码 */
void shell_err_md5(const char *file);     /* md5 哈希 */
void shell_err_exec(const char *file);    /* exec 执行程序 */
void shell_err_nohup(void);              /* nohup 后台运行 */

/* 系统指令错误 */
void shell_err_ping(const char *arg);   /* ping 网络测试 */
void shell_err_kill(const char *pid);   /* kill 杀进程 */
void shell_err_mount(const char *dev);  /* mount 挂载 */
void shell_err_umount(const char *dir); /* umount 卸载 */
void shell_err_format(void);        /* format 格式化 */
void shell_err_fdisk(void);         /* fdisk 分区 */
void shell_err_chkdsk(void);        /* chkdsk 磁盘检查 */
void shell_err_calc(void);          /* calc 计算器 */
void shell_err_fg(const char *pid); /* fg 前台 */
void shell_err_bg(const char *pid); /* bg 后台 */
void shell_err_nice(void);          /* nice 优先级 */
void shell_err_renice(void);        /* renice 调优先级 */
void shell_err_watch(void);         /* watch 监视 */
void shell_err_sleep(void);         /* sleep 睡眠 */
void shell_err_test(void);          /* test 测试 */
void shell_err_expr(void);          /* expr 表达式 */
void shell_err_xargs(void);         /* xargs 参数构建 */
void shell_err_tee(const char *file);   /* tee */
void shell_err_install(void);       /* install 安装 */
void shell_err_which(const char *cmd);  /* which 查找指令 */
void shell_err_pkg(const char *subcmd); /* pkg 包管理 */
void shell_err_httpget(void);       /* httpget HTTP请求 */
void shell_err_imgview(void);       /* imgview 图片查看 */
void shell_err_freq(void);          /* freq CPU频率 */
void shell_err_fw(void);            /* fw 固件 */
void shell_err_kvm(void);           /* kvm 虚拟化 */
void shell_err_crepl(void);         /* c/crepl C解释器 */
void shell_err_alias(const char *arg);  /* alias 别名 */
void shell_err_help(const char *arg);   /* help 帮助 */
void shell_err_logrotate(void);     /* logrotate 日志轮转 */
void shell_err_syslog(void);        /* syslog 系统日志 */

/* 未知指令错误 */
void shell_err_unknown(const char *cmd);

#endif /* SHELL_ERROR_H */
