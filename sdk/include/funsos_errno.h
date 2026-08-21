#ifndef FUNSOS_ERRNO_H
#define FUNSOS_ERRNO_H

/*
 * FUNSOS 错误码增强 API
 * 提供更完整的错误码定义和错误信息查询功能。
 * 基于 kernel/sys_api.h 的系统调用封装。
 */

#include "stdint.h"

/* ---- 扩展错误码定义 ---- */

/* 通用错误码 (0 - -19) */
#define FUNSOS_EPERM           -1     /* 操作不允许 */
#define FUNSOS_ENOENT          -2     /* 文件或目录不存在 */
#define FUNSOS_ESRCH           -3     /* 进程不存在 */
#define FUNSOS_EINTR           -4     /* 系统调用被中断 */
#define FUNSOS_EIO             -5     /* I/O 错误 */
#define FUNSOS_ENXIO           -6     /* 设备或地址不存在 */
#define FUNSOS_E2BIG           -7     /* 参数列表过长 */
#define FUNSOS_ENOEXEC         -8     /* 可执行文件格式错误 */
#define FUNSOS_EBADF           -9     /* 无效的文件描述符 */
#define FUNSOS_ECHILD          -10    /* 无子进程 */
#define FUNSOS_EAGAIN          -11    /* 资源暂时不可用 */
#define FUNSOS_ENOMEM          -12    /* 内存不足 */
#define FUNSOS_EACCES          -13    /* 权限不足 */
#define FUNSOS_EFAULT          -14    /* 地址错误 */
#define FUNSOS_ENOTBLK         -15    /* 不是块设备 */
#define FUNSOS_EBUSY           -16    /* 设备或资源忙 */
#define FUNSOS_EEXIST          -17    /* 文件已存在 */
#define FUNSOS_EXDEV           -18    /* 跨设备链接 */
#define FUNSOS_ENODEV          -19    /* 设备不存在 */

/* 文件系统错误码 (-20 - -39) */
#define FUNSOS_ENOTDIR         -20    /* 不是目录 */
#define FUNSOS_EISDIR          -21    /* 是目录 */
#define FUNSOS_EINVAL          -22    /* 无效参数 */
#define FUNSOS_ENFILE          -23    /* 系统打开文件数过多 */
#define FUNSOS_EMFILE          -24    /* 进程打开文件数过多 */
#define FUNSOS_ENOTTY          -25    /* 不是终端设备 */
#define FUNSOS_ETXTBSY         -26    /* 文本文件忙 */
#define FUNSOS_EFBIG           -27    /* 文件过大 */
#define FUNSOS_ENOSPC          -28    /* 设备无剩余空间 */
#define FUNSOS_ESPIPE          -29    /* 非法的地址偏移 */
#define FUNSOS_EROFS           -30    /* 只读文件系统 */
#define FUNSOS_EMLINK          -31    /* 链接数过多 */
#define FUNSOS_EPIPE           -32    /* 管道破裂 */
#define FUNSOS_EDOM            -33    /* 数值超出范围 */
#define FUNSOS_ERANGE          -34    /* 结果范围错误 */
#define FUNSOS_EDEADLK         -35    /* 资源死锁 */
#define FUNSOS_ENAMETOOLONG    -36    /* 文件名过长 */
#define FUNSOS_ENOLCK          -37    /* 没有可用的锁 */
#define FUNSOS_ENOSYS          -38    /* 功能未实现 */
#define FUNSOS_ENOTEMPTY       -39    /* 目录非空 */

/* 网络/进程错误码 (-40 - -59) */
#define FUNSOS_ELOOP           -40    /* 符号链接循环 */
#define FUNSOS_ENOMSG          -42    /* 没有消息 */
#define FUNSOS_EIDRM           -43    /* 标识符被删除 */
#define FUNSOS_ECHRNG          -44    /* 通道数超出范围 */
#define FUNSOS_EL2NSYNC        -45    /* 2级不同步 */
#define FUNSOS_EL3HLT          -46    /* 3级停止 */
#define FUNSOS_EL3RST          -47    /* 3级重置 */
#define FUNSOS_ELNRNG          -48    /* 链接数超出范围 */
#define FUNSOS_EUNATCH         -49    /* 协议驱动未连接 */
#define FUNSOS_ENOCSI          -50    /* 没有CSI结构 */
#define FUNSOS_EL2HLT          -51    /* 2级停止 */
#define FUNSOS_EBADE           -52    /* 无效的交换 */
#define FUNSOS_EBADR           -53    /* 无效的请求描述符 */
#define FUNSOS_EXFULL          -54    /* 交换已满 */
#define FUNSOS_ENOANO          -55    /* 无阳极 */
#define FUNSOS_EBADRQC         -56    /* 无效的请求码 */
#define FUNSOS_EBADSLT         -57    /* 无效的槽 */
#define FUNSOS_EDEADLOCK       -58    /* 死锁 */
#define FUNSOS_EBFONT          -59    /* 字体文件格式错误 */

/* 网络错误码 (-60 - -99) */
#define FUNSOS_ENONET          -64    /* 机器不在网络上 */
#define FUNSOS_ENOPKG          -65    /* 包未安装 */
#define FUNSOS_EREMOTE         -66    /* 对象是远程的 */
#define FUNSOS_ENOLINK         -67    /* 链接已中断 */
#define FUNSOS_EADV            -68    /* 播送错误 */
#define FUNSOS_ESRMNT          -69    /* Srmount 错误 */
#define FUNSOS_ECOMM           -70    /* 通信错误 */
#define FUNSOS_EPROTO          -71    /* 协议错误 */
#define FUNSOS_EMULTIHOP       -72    /* 多跳 */
#define FUNSOS_EDOTDOT         -73    /* RFS 特定错误 */
#define FUNSOS_EBADMSG         -74    /* 不是数据消息 */
#define FUNSOS_EOVERFLOW       -75    /* 值过大 */
#define FUNSOS_ENOTUNIQ        -76    /* 名称在网络上不唯一 */
#define FUNSOS_EBADFD          -77    /* 文件描述符状态错误 */
#define FUNSOS_EREMCHG         -78    /* 远程地址更改 */
#define FUNSOS_ELIBACC         -79    /* 无法访问共享库 */
#define FUNSOS_ELIBBAD         -80    /* 访问损坏的共享库 */
#define FUNSOS_ELIBSCN         -81    /* .lib 节损坏 */
#define FUNSOS_ELIBMAX         -82    /* 尝试链接过多的共享库 */
#define FUNSOS_ELIBEXEC        -83    /* 无法直接执行共享库 */
#define FUNSOS_EILSEQ          -84    /* 非法字节序列 */
#define FUNSOS_ERESTART        -85    /* 中断的系统调用应重新启动 */
#define FUNSOS_ESTRPIPE        -86    /* 流管道错误 */
#define FUNSOS_EUSERS          -87    /* 用户太多 */
#define FUNSOS_ENOTSOCK        -88    /* 不是套接字 */
#define FUNSOS_EDESTADDRREQ    -89    /* 需要目标地址 */
#define FUNSOS_EMSGSIZE        -90    /* 消息过大 */
#define FUNSOS_EPROTOTYPE      -91    /* 协议类型错误 */
#define FUNSOS_ENOPROTOOPT     -92    /* 协议不可用 */
#define FUNSOS_EPROTONOSUPPORT -93    /* 不支持的协议 */
#define FUNSOS_ESOCKTNOSUPPORT -94    /* 不支持的套接字类型 */
#define FUNSOS_EOPNOTSUPP      -95    /* 操作不支持 */
#define FUNSOS_EPFNOSUPPORT    -96    /* 不支持的协议族 */
#define FUNSOS_EAFNOSUPPORT    -97    /* 地址族不支持 */
#define FUNSOS_EADDRINUSE      -98    /* 地址已被使用 */
#define FUNSOS_EADDRNOTAVAIL   -99    /* 地址不可用 */

/* 更多网络错误码 (-100 - -120) */
#define FUNSOS_ENETDOWN        -100   /* 网络已关闭 */
#define FUNSOS_ENETUNREACH     -101   /* 网络不可达 */
#define FUNSOS_ENETRESET       -102   /* 网络重置 */
#define FUNSOS_ECONNABORTED    -103   /* 连接中止 */
#define FUNSOS_ECONNRESET      -104   /* 连接重置 */
#define FUNSOS_ENOBUFS         -105   /* 没有可用的缓冲区 */
#define FUNSOS_EISCONN         -106   /* 套接字已连接 */
#define FUNSOS_ENOTCONN        -107   /* 套接字未连接 */
#define FUNSOS_ESHUTDOWN       -108   /* 发送后传输端点已关闭 */
#define FUNSOS_ETOOMANYREFS    -109   /* 引用过多 */
#define FUNSOS_ETIMEDOUT       -110   /* 连接超时 */
#define FUNSOS_ECONNREFUSED    -111   /* 连接被拒绝 */
#define FUNSOS_EHOSTDOWN       -112   /* 主机已关闭 */
#define FUNSOS_EHOSTUNREACH    -113   /* 主机不可达 */
#define FUNSOS_EALREADY        -114   /* 操作已在进行中 */
#define FUNSOS_EINPROGRESS     -115   /* 操作正在进行中 */
#define FUNSOS_ESTALE          -116   /* 过时的文件句柄 */
#define FUNSOS_EUCLEAN         -117   /* 结构需要清理 */
#define FUNSOS_ENOTNAM         -118   /* 不是XENIX命名文件 */
#define FUNSOS_ENAVAIL         -119   /* 没有XENIX信号量 */
#define FUNSOS_EISNAM          -120   /* 是命名文件 */

/* 线程/同步错误码 (-121 - -140) */
#define FUNSOS_EREMOTEIO       -121   /* 远程I/O错误 */
#define FUNSOS_EDQUOT          -122   /* 超出磁盘配额 */
#define FUNSOS_ENOMEDIUM       -123   /* 没有找到介质 */
#define FUNSOS_EMEDIUMTYPE     -124   /* 介质类型错误 */
#define FUNSOS_ECANCELED       -125   /* 操作已取消 */
#define FUNSOS_ENOKEY          -126   /* 所需的密钥不可用 */
#define FUNSOS_EKEYEXPIRED     -127   /* 密钥已过期 */
#define FUNSOS_EKEYREVOKED     -128   /* 密钥已撤销 */
#define FUNSOS_EKEYREJECTED    -129   /* 密钥被拒绝 */
#define FUNSOS_EOWNERDEAD      -130   /* 所有者已死 */
#define FUNSOS_ENOTRECOVERABLE -131   /* 状态不可恢复 */
#define FUNSOS_ERFKILL         -132   /* 由于射频终止操作不可能 */

/* 最大错误码数量 */
#define FUNSOS_ERRNO_MAX       256

/*
 * 获取当前线程的错误码
 * 返回: 当前错误码值
 */
int funsos_get_errno(void);

/*
 * 设置当前线程的错误码
 * 参数: errnum - 错误码值
 */
void funsos_set_errno(int errnum);

/*
 * 获取错误码对应的描述字符串
 * 参数: errnum - 错误码值
 * 返回: 错误描述字符串指针
 */
const char *funsos_strerror(int errnum);

/*
 * 打印错误信息到标准错误
 * 参数: s - 自定义前缀字符串
 */
void funsos_perror(const char *s);

#endif /* FUNSOS_ERRNO_H */
