/* funpack.c - .FUN 文件打包工具
 * 用于创建、列出、提取 .FUN 归档文件的命令行工具
 *
 * 用法:
 *   funpack -c <archive.fun> <file1> <file2> ...  创建归档
 *   funpack -l <archive.fun>                       列出归档内容
 *   funpack -x <archive.fun>                       提取所有文件
 *   funpack -x <archive.fun> <file>                提取指定文件
 *   funpack -v <archive.fun>                       验证归档完整性
 */

#include "funpack.h"
#include "funsos.h"

/* ---- 全局状态 ---- */
static char g_error_buf[256];

/* 简单的哈希函数 (Fletcher-32) */
static uint32_t funpack_fletcher32(const uint8_t *data, uint32_t len)
{
    uint32_t sum1 = 0, sum2 = 0;
    for (uint32_t i = 0; i < len; i++) {
        sum1 = (sum1 + data[i]) % 65535;
        sum2 = (sum2 + sum1) % 65535;
    }
    return (sum2 << 16) | sum1;
}

/* 计算简单校验和 */
static uint32_t funpack_checksum(const uint8_t *data, uint32_t len)
{
    uint32_t sum = 0;
    for (uint32_t i = 0; i < len; i++)
        sum += data[i];
    return sum;
}

/* 复制字符串 */
static void funpack_strcpy(char *dst, const char *src, int max_len)
{
    int i = 0;
    while (i < max_len - 1 && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* 字符串长度 */
static int funpack_strlen(const char *s)
{
    int len = 0;
    while (s[len]) len++;
    return len;
}

/* 字符串比较 */
static int funpack_strcmp(const char *a, const char *b)
{
    while (*a && *b && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

/* ---- 打包器 API ---- */

int funpack_create(const char *archive_path, const char **files, int file_count)
{
    if (archive_path == NULL || files == NULL || file_count <= 0) {
        funpack_strcpy(g_error_buf, "Invalid parameters", sizeof(g_error_buf));
        return -1;
    }

    /* 打开输出文件 */
    funsos_file_t out = funsos_file_open(archive_path, FUNSOS_FILE_CREATE | FUNSOS_FILE_WRITE);
    if (out == NULL) {
        funpack_strcpy(g_error_buf, "Cannot create archive file", sizeof(g_error_buf));
        return -1;
    }

    /* 准备文件头 */
    funpack_header_t header;
    header.magic = FUNPACK_MAGIC;
    header.version = FUNPACK_VERSION;
    header.file_count = file_count;
    header.index_offset = sizeof(funpack_header_t);
    header.data_offset = sizeof(funpack_header_t) + file_count * sizeof(funpack_entry_t);
    funpack_strcpy(header.description, "Created by funpack tool", 64);

    /* 写入文件头 */
    funsos_file_write(out, &header, sizeof(funpack_header_t));

    /* 准备条目表 */
    funpack_entry_t *entries = (funpack_entry_t *)funs_malloc(sizeof(funpack_entry_t) * file_count);
    uint32_t current_offset = 0;

    /* 遍历文件，收集信息 */
    for (int i = 0; i < file_count; i++) {
        funpack_entry_t *entry = &entries[i];
        funpack_strcpy(entry->name, files[i], 64);

        /* 获取文件大小 */
        funsos_file_t f = funsos_file_open(files[i], FUNSOS_FILE_READ);
        if (f) {
            entry->size = funsos_file_size(f);
            entry->compressed_size = 0;
            funsos_file_close(f);
        } else {
            entry->size = 0;
            entry->compressed_size = 0;
        }

        entry->offset = current_offset;
        entry->flags = 0;
        entry->checksum = 0;

        current_offset += entry->size;
    }

    /* 写入条目表 */
    funsos_file_write(out, entries, sizeof(funpack_entry_t) * file_count);

    /* 写入文件数据 */
    for (int i = 0; i < file_count; i++) {
        funsos_file_t f = funsos_file_open(files[i], FUNSOS_FILE_READ);
        if (f) {
            uint8_t *data = (uint8_t *)funs_malloc(entries[i].size);
            int n = funsos_file_read(f, data, entries[i].size);
            if (n > 0) {
                funsos_file_write(out, data, n);
                entries[i].checksum = funpack_fletcher32(data, n);
            }
            funs_free(data);
            funsos_file_close(f);
        }

        /* 更新条目表头部中的校验和 */
        if (entries[i].checksum != 0) {
            /* 回写校验和到条目表 */
            uint32_t current_pos = funsos_file_tell(out);
            funsos_file_seek(out, sizeof(funpack_header_t) + i * sizeof(funpack_entry_t));
            funsos_file_write(out, entries, sizeof(funpack_entry_t) * file_count);
            funsos_file_seek(out, current_pos);
        }
    }

    funs_free(entries);
    funsos_file_close(out);
    return 0;
}

const char *funpack_get_error(void)
{
    return g_error_buf;
}

int funpack_list(const char *archive_path, funpack_entry_t *entries, int max_entries)
{
    if (archive_path == NULL) {
        funpack_strcpy(g_error_buf, "Invalid archive path", sizeof(g_error_buf));
        return -1;
    }

    funsos_file_t f = funsos_file_open(archive_path, FUNSOS_FILE_READ);
    if (f == NULL) {
        funpack_strcpy(g_error_buf, "Cannot open archive file", sizeof(g_error_buf));
        return -1;
    }

    /* 读取文件头 */
    funpack_header_t header;
    int n = funsos_file_read(f, &header, sizeof(funpack_header_t));
    if (n != sizeof(funpack_header_t) || header.magic != FUNPACK_MAGIC) {
        funpack_strcpy(g_error_buf, "Invalid .FUN archive format", sizeof(g_error_buf));
        funsos_file_close(f);
        return -1;
    }

    int count = (int)header.file_count;
    if (count > max_entries) count = max_entries;

    /* 读取条目表 */
    funsos_file_seek(f, (int)header.index_offset);
    funsos_file_read(f, entries, count * sizeof(funpack_entry_t));

    funsos_file_close(f);
    return count;
}

int funpack_extract(const char *archive_path, const char *output_dir, const char *filename)
{
    if (archive_path == NULL || output_dir == NULL) {
        funpack_strcpy(g_error_buf, "Invalid parameters", sizeof(g_error_buf));
        return -1;
    }

    funsos_file_t f = funsos_file_open(archive_path, FUNSOS_FILE_READ);
    if (f == NULL) {
        funpack_strcpy(g_error_buf, "Cannot open archive file", sizeof(g_error_buf));
        return -1;
    }

    /* 读取文件头 */
    funpack_header_t header;
    int n = funsos_file_read(f, &header, sizeof(funpack_header_t));
    if (n != sizeof(funpack_header_t) || header.magic != FUNPACK_MAGIC) {
        funpack_strcpy(g_error_buf, "Invalid .FUN archive format", sizeof(g_error_buf));
        funsos_file_close(f);
        return -1;
    }

    /* 读取条目表 */
    int count = (int)header.file_count;
    funpack_entry_t *entries = (funpack_entry_t *)funs_malloc(sizeof(funpack_entry_t) * count);
    funsos_file_seek(f, (int)header.index_offset);
    funsos_file_read(f, entries, count * sizeof(funpack_entry_t));

    int extracted = 0;
    for (int i = 0; i < count; i++) {
        if (filename != NULL && funpack_strcmp(entries[i].name, filename) != 0)
            continue;

        /* 构建输出路径 */
        char out_path[256];
        funpack_strcpy(out_path, output_dir, sizeof(out_path));
        int out_len = funpack_strlen(out_path);
        if (out_path[out_len - 1] != '/') {
            out_path[out_len] = '/';
            out_len++;
        }
        funpack_strcpy(out_path + out_len, entries[i].name, sizeof(out_path) - out_len);

        /* 提取文件 */
        funsos_file_t out = funsos_file_open(out_path, FUNSOS_FILE_CREATE | FUNSOS_FILE_WRITE);
        if (out) {
            uint8_t *data = (uint8_t *)funs_malloc(entries[i].size);
            uint32_t data_offset = header.data_offset + entries[i].offset;
            funsos_file_seek(f, (int)data_offset);
            int read_n = funsos_file_read(f, data, entries[i].size);
            if (read_n > 0) {
                funsos_file_write(out, data, read_n);
            }
            funs_free(data);
            funsos_file_close(out);
            extracted++;
        }
    }

    funs_free(entries);
    funsos_file_close(f);
    return extracted;
}

int funpack_verify(const char *archive_path)
{
    if (archive_path == NULL) {
        funpack_strcpy(g_error_buf, "Invalid archive path", sizeof(g_error_buf));
        return -1;
    }

    funsos_file_t f = funsos_file_open(archive_path, FUNSOS_FILE_READ);
    if (f == NULL) {
        funpack_strcpy(g_error_buf, "Cannot open archive file", sizeof(g_error_buf));
        return -1;
    }

    /* 读取文件头 */
    funpack_header_t header;
    int n = funsos_file_read(f, &header, sizeof(funpack_header_t));
    if (n != sizeof(funpack_header_t) || header.magic != FUNPACK_MAGIC) {
        funpack_strcpy(g_error_buf, "Invalid magic number", sizeof(g_error_buf));
        funsos_file_close(f);
        return -1;
    }

    if (header.version > FUNPACK_VERSION) {
        funpack_strcpy(g_error_buf, "Archive version too new", sizeof(g_error_buf));
        funsos_file_close(f);
        return -1;
    }

    /* 读取条目表 */
    int count = (int)header.file_count;
    funpack_entry_t *entries = (funpack_entry_t *)funs_malloc(sizeof(funpack_entry_t) * count);
    funsos_file_seek(f, (int)header.index_offset);
    funsos_file_read(f, entries, count * sizeof(funpack_entry_t));

    int errors = 0;
    for (int i = 0; i < count; i++) {
        uint8_t *data = (uint8_t *)funs_malloc(entries[i].size);
        uint32_t data_offset = header.data_offset + entries[i].offset;
        funsos_file_seek(f, (int)data_offset);
        int read_n = funsos_file_read(f, data, entries[i].size);
        if (read_n > 0) {
            uint32_t calc_csum = funpack_fletcher32(data, read_n);
            if (calc_csum != entries[i].checksum && entries[i].checksum != 0) {
                errors++;
            }
        }
        funs_free(data);
    }

    funs_free(entries);
    funsos_file_close(f);

    return errors;
}