#ifndef ERRNO_H
#define ERRNO_H

#include "stdint.h"

/* ============================================================
 * POSIX 标准错误代码 (1-133)
 * ============================================================ */

#define EPERM            1    /* Operation not permitted */
#define ENOENT           2    /* No such file or directory */
#define ESRCH            3    /* No such process */
#define EINTR            4    /* Interrupted system call */
#define EIO              5    /* I/O error */
#define ENXIO            6    /* No such device or address */
#define E2BIG            7    /* Argument list too long */
#define ENOEXEC          8    /* Exec format error */
#define EBADF            9    /* Bad file number */
#define ECHILD          10    /* No child processes */
#define EAGAIN          11    /* Try again */
#define ENOMEM          12    /* Out of memory */
#define EACCES          13    /* Permission denied */
#define EFAULT          14    /* Bad address */
#define ENOTBLK         15    /* Block device required */
#define EBUSY           16    /* Device or resource busy */
#define EEXIST          17    /* File exists */
#define EXDEV           18    /* Cross-device link */
#define ENODEV          19    /* No such device */
#define ENOTDIR         20    /* Not a directory */
#define EISDIR          21    /* Is a directory */
#define EINVAL          22    /* Invalid argument */
#define ENFILE          23    /* File table overflow */
#define EMFILE          24    /* Too many open files */
#define ENOTTY          25    /* Not a typewriter */
#define ETXTBSY         26    /* Text file busy */
#define EFBIG           27    /* File too large */
#define ENOSPC          28    /* No space left on device */
#define ESPIPE          29    /* Illegal seek */
#define EROFS           30    /* Read-only file system */
#define EMLINK          31    /* Too many links */
#define EPIPE           32    /* Broken pipe */
#define EDOM            33    /* Math argument out of domain of func */
#define ERANGE          34    /* Math result not representable */
#define EDEADLK         35    /* Resource deadlock would occur */
#define ENAMETOOLONG    36    /* File name too long */
#define ENOLCK          37    /* No record locks available */
#define ENOSYS          38    /* Function not implemented */
#define ENOTEMPTY       39    /* Directory not empty */
#define ELOOP           40    /* Too many symbolic links encountered */
#define EWOULDBLOCK     EAGAIN  /* Operation would block */
#define ENOMSG          42    /* No message of desired type */
#define EIDRM           43    /* Identifier removed */
#define ECHRNG          44    /* Channel number out of range */
#define EL2NSYNC        45    /* Level 2 not synchronized */
#define EL3HLT          46    /* Level 3 halted */
#define EL3RST          47    /* Level 3 reset */
#define ELNRNG          48    /* Link number out of range */
#define EUNATCH         49    /* Protocol driver not attached */
#define ENOCSI          50    /* No CSI structure available */
#define EL2HLT          51    /* Level 2 halted */
#define EBADE           52    /* Invalid exchange */
#define EBADR           53    /* Invalid request descriptor */
#define EXFULL          54    /* Exchange full */
#define ENOANO          55    /* No anode */
#define EBADRQC         56    /* Invalid request code */
#define EBADSLT         57    /* Invalid slot */
#define EDEADLOCK       EDEADLK
#define EBFONT          59    /* Bad font file format */
#define ENOSTR          60    /* Device not a stream */
#define ENODATA         61    /* No data available */
#define ETIME           62    /* Timer expired */
#define ENOSR           63    /* Out of streams resources */
#define ENONET          64    /* Machine is not on the network */
#define ENOPKG          65    /* Package not installed */
#define EREMOTE         66    /* Object is remote */
#define ENOLINK         67    /* Link has been severed */
#define EADV            68    /* Advertise error */
#define ESRMNT          69    /* Srmount error */
#define ECOMM           70    /* Communication error on send */
#define EPROTO          71    /* Protocol error */
#define EMULTIHOP       72    /* Multihop attempted */
#define EDOTDOT         73    /* RFS specific error */
#define EBADMSG         74    /* Not a data message */
#define EOVERFLOW       75    /* Value too large for defined data type */
#define ENOTUNIQ        76    /* Name not unique on network */
#define EBADFD          77    /* File descriptor in bad state */
#define EREMCHG         78    /* Remote address changed */
#define ELIBACC         79    /* Can not access a needed shared library */
#define ELIBBAD         80    /* Accessing a corrupted shared library */
#define ELIBSCN         81    /* .lib section in a.out corrupted */
#define ELIBMAX         82    /* Attempting to link in too many shared libraries */
#define ELIBEXEC        83    /* Cannot exec a shared library directly */
#define EILSEQ          84    /* Illegal byte sequence */
#define ERESTART        85    /* Interrupted system call should be restarted */
#define ESTRPIPE        86    /* Streams pipe error */
#define EUSERS          87    /* Too many users */
#define ENOTSOCK        88    /* Socket operation on non-socket */
#define EDESTADDRREQ    89    /* Destination address required */
#define EMSGSIZE        90    /* Message too long */
#define EPROTOTYPE      91    /* Protocol wrong type for socket */
#define ENOPROTOOPT     92    /* Protocol not available */
#define EPROTONOSUPPORT 93    /* Protocol not supported */
#define ESOCKTNOSUPPORT 94    /* Socket type not supported */
#define EOPNOTSUPP      95    /* Operation not supported on transport endpoint */
#define EPFNOSUPPORT    96    /* Protocol family not supported */
#define EAFNOSUPPORT    97    /* Address family not supported by protocol */
#define EADDRINUSE      98    /* Address already in use */
#define EADDRNOTAVAIL   99    /* Cannot assign requested address */
#define ENETDOWN       100    /* Network is down */
#define ENETUNREACH    101    /* Network is unreachable */
#define ENETRESET      102    /* Network dropped connection because of reset */
#define ECONNABORTED   103    /* Software caused connection abort */
#define ECONNRESET     104    /* Connection reset by peer */
#define ENOBUFS        105    /* No buffer space available */
#define EISCONN        106    /* Transport endpoint is already connected */
#define ENOTCONN       107    /* Transport endpoint is not connected */
#define ESHUTDOWN      108    /* Cannot send after transport endpoint shutdown */
#define ETOOMANYREFS   109    /* Too many references: cannot splice */
#define ETIMEDOUT      110    /* Connection timed out */
#define ECONNREFUSED   111    /* Connection refused */
#define EHOSTDOWN      112    /* Host is down */
#define EHOSTUNREACH   113    /* No route to host */
#define EALREADY       114    /* Operation already in progress */
#define EINPROGRESS    115    /* Operation now in progress */
#define ESTALE         116    /* Stale NFS file handle */
#define EUCLEAN        117    /* Structure needs cleaning */
#define ENOTNAM        118    /* Not a XENIX named type file */
#define ENAVAIL        119    /* No XENIX semaphores available */
#define EISNAM         120    /* Is a named type file */
#define EREMOTEIO      121    /* Remote I/O error */
#define EDQUOT         122    /* Quota exceeded */
#define ENOMEDIUM      123    /* No medium found */
#define EMEDIUMTYPE    124    /* Wrong medium type */
#define ECANCELED      125    /* Operation Canceled */
#define ENOKEY         126    /* Required key not available */
#define EKEYEXPIRED    127    /* Key has expired */
#define EKEYREVOKED    128    /* Key has been revoked */
#define EKEYREJECTED   129    /* Key was rejected by service */
#define EOWNERDEAD     130    /* Owner died */
#define ENOTRECOVERABLE 131   /* State not recoverable */
#define ERFKILL        132    /* Operation not possible due to RF-kill */
#define EHWPOISON      133    /* Memory page has hardware error */

/* ============================================================
 * 自定义内核错误码 (200-299)
 * ============================================================ */

#define EKEXEC         200    /* Kernel execution error */
#define EPANIC         201    /* Kernel panic */
#define EOOM           202    /* Out of kernel memory */
#define EINVALPROC     203    /* Invalid process */
#define EINVALTHR      204    /* Invalid thread */
#define EINVALMEM      205    /* Invalid memory region */
#define EINVALFS       206    /* Invalid filesystem */
#define EINVALMOD      207    /* Invalid kernel module */
#define EMODNOTFOUND   208    /* Module not found */
#define EMODDEP        209    /* Module dependency error */
#define EMOUNT         210    /* Mount error */
#define EUMOUNT        211    /* Unmount error */
#define ESYSCALL       212    /* Invalid syscall */
#define ESIGNAL        213    /* Signal delivery error */
#define ESCHED         214    /* Scheduler error */
#define EIPC           215    /* IPC error */
#define ESHM           216    /* Shared memory error */
#define EMSGQ          217    /* Message queue error */
#define ESEM           218    /* Semaphore error */
#define EFUTEX         219    /* Futex error */
#define EEPOLL         220    /* Epoll error */
#define EINOTIFY       221    /* Inotify error */
#define ECGROUP        222    /* Cgroup error */
#define ENAMESPACE     223    /* Namespace error */
#define ECAPABILITY    224    /* Capability error */
#define EAUDIT         225    /* Audit error */
#define ESELINUX       226    /* SELinux error */
#define EAPPARMOR      227    /* AppArmor error */
#define ESECURITY      228    /* Security policy violation */
#define ECRYPT         229    /* Encryption error */
#define ECOMPRESS      230    /* Compression error */
#define ECHECKSUM      231    /* Checksum error */
#define ECRC           232    /* CRC error */
#define EHASH          233    /* Hash error */
#define ESIGN          234    /* Signature verification error */
#define ECERT          235    /* Certificate error */
#define ETLS           236    /* TLS/SSL error */
#define EDNS           237    /* DNS resolution error */
#define EDHCP          238    /* DHCP error */
#define EFIREWALL      239    /* Firewall rule error */
#define ENAT           240    /* NAT error */
#define ERoute         241    /* Routing error */
#define EARP           242    /* ARP error */
#define EICMP          243    /* ICMP error */
#define ETCP           244    /* TCP error */
#define EUDP           245    /* UDP error */
#define ERAW           246    /* Raw socket error */
#define EPACKET        247    /* Packet socket error */
#define ENETLINK       248    /* Netlink error */
#define EVPN           249    /* VPN error */
#define EPROXY         250    /* Proxy error */
#define ETUNNEL        251    /* Tunnel error */
#define EBRIDGE        252    /* Bridge error */
#define EVLAN          253    /* VLAN error */
#define EBOND          254    /* Bonding error */
#define ETEAM          255    /* Team device error */
#define EMCAST         256    /* Multicast error */
#define EBCAST         257    /* Broadcast error */
#define EQOS           258    /* QoS error */
#define ETC            259    /* Traffic control error */
#define EBQL           260    /* Byte queue limits error */
#define EXDP           261    /* XDP error */
#define EBpf           262    /* BPF program error */
#define ETRACE         263    /* Tracing error */
#define EPROBE         264    /* Kprobe error */
#define EDEBUG         265    /* Debug error */
#define EPROFILE       266    /* Profiling error */
#define ECOREDUMP      267    /* Core dump error */
#define EKSTACK        268    /* Kernel stack overflow */
#define EUSTACK        269    /* User stack overflow */
#define EDOUBLEFAULT   270    /* Double fault */
#define EGPF           271    /* General protection fault */
#define EPAGEFAULT     272    /* Page fault */
#define ESEGFAULT      273    /* Segmentation fault */
#define EBUSERR        274    /* Bus error */
#define EILLEGAL       275    /* Illegal instruction */
#define EDIVZERO       276    /* Division by zero */
#define EBOUND         277    /* Bound range exceeded */
#define EOVERFLOW_EXC  278    /* Overflow exception */
#define EDEVNOTFOUND   279    /* Device not found */
#define EDEVBUSY       280    /* Device busy */
#define EDEVFAIL       281    /* Device failure */
#define EDEVNOTSUPP    282    /* Device not supported */
#define EDEVTIMEOUT    283    /* Device timeout */
#define EHW            284    /* Hardware error */
#define EFIRMWARE      285    /* Firmware error */
#define EBIOS          286    /* BIOS error */
#define EACPI_         287    /* ACPI error */
#define ESMBIOS        288    /* SMBIOS error */
#define EPCI           289    /* PCI error */
#define EPCIE          290    /* PCIe error */
#define EUSB           291    /* USB error */
#define ESATA          292    /* SATA error */
#define ESCSI          293    /* SCSI error */
#define ENVME          294    /* NVMe error */
#define EMMC           295    /* MMC/SD error */
#define EIDE           296    /* IDE error */
#define EFLOPPY        297    /* Floppy error */
#define ETAPE          298    /* Tape error */
#define ECDROM         299    /* CDROM error */

#define MAX_ERRNO     4096   /* Maximum errno value */

/* ============================================================
 * 全局 errno 变量 (用户态)
 * ============================================================ */

#ifndef __KERNEL__
extern int errno;
#endif

/* ============================================================
 * 错误码名称查询函数 (实现在 string.c / errno.c)
 * ============================================================ */

const char *errno_name(int errnum);

/* 内核内部使用的错误码指针 */
#define ERR_PTR(err)  ((void *)(long)(err))
#define PTR_ERR(ptr)  ((long)(ptr))
#define IS_ERR(ptr)   ((unsigned long)(ptr) > (unsigned long)-MAX_ERRNO)

#endif /* ERRNO_H */
