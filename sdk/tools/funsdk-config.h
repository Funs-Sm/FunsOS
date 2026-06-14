/* funsdk-config.h - SDK configuration
 * Build-time configuration for the FUNSOS SDK
 */

#ifndef FUNSDK_CONFIG_H
#define FUNSDK_CONFIG_H

/* SDK version */
#define FUNSDK_VERSION_MAJOR  1
#define FUNSDK_VERSION_MINOR  0
#define FUNSDK_VERSION_PATCH  0
#define FUNSDK_VERSION_STRING "1.0.0"

/* Target architecture */
#define FUNSDK_ARCH_X86       1
#define FUNSDK_BITS           32

/* Feature flags */
#define FUNSDK_ENABLE_3D      1
#define FUNSDK_ENABLE_AUDIO   1
#define FUNSDK_ENABLE_NETWORK 1
#define FUNSDK_ENABLE_THREADS 1
#define FUNSDK_ENABLE_DATABASE 1

/* Memory configuration */
#define FUNSDK_HEAP_SIZE      (4 * 1024 * 1024)  /* 4 MB */
#define FUNSDK_STACK_SIZE     (64 * 1024)         /* 64 KB */
#define FUNSDK_MAX_WINDOWS    32
#define FUNSDK_MAX_SOCKETS    64
#define FUNSDK_MAX_FILES      128

/* Graphics configuration */
#define FUNSDK_DEFAULT_WIDTH  800
#define FUNSDK_DEFAULT_HEIGHT 600
#define FUNSDK_COLOR_DEPTH    32

/* Network configuration */
#define FUNSDK_MAX_CONNECTIONS 16
#define FUNSDK_SOCKET_BUFFER   8192

/* Build options */
#define FUNSDK_DEBUG          0
#define FUNSDK_OPTIMIZE       2

#endif /* FUNSDK_CONFIG_H */
