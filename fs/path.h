#ifndef PATH_H
#define PATH_H

#include "vfs.h"

#define PATH_MAX 4096

int32_t path_resolve(const char *path, dentry_t **out);
int32_t path_normalize(const char *path, char *out, uint32_t out_size);
int32_t path_parent(const char *path, char *out, uint32_t out_size);
int32_t path_basename(const char *path, char *out, uint32_t out_size);
int32_t path_join(const char *dir, const char *name, char *out, uint32_t out_size);

#endif
