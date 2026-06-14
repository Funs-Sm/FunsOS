/* bios_edit.c - BIOS 固件编辑系统实现
 * 读写真实 BIOS 数据区（物理内存 0xF0000-0xFFFFF，64KB）
 * 通过 vmm_map_physical 映射后直接读取
 * 写入操作必须有安全确认
 */

#include "bios_edit.h"
#include "vmm.h"
#include "kheap.h"
#include "string.h"
#include "stdio.h"
#include "shell.h"
#include "keyboard.h"
#include "stdlib.h"

/* 映射后的 BIOS 虚拟地址 */
static uint8_t *bios_mapped = 0;
static int bios_initialized = 0;

void bios_edit_init(void) {
    if (bios_initialized) return;
    /* 映射 BIOS ROM 区域到内核虚拟地址空间 */
    bios_mapped = (uint8_t *)vmm_map_physical(BIOS_ROM_BASE, BIOS_ROM_SIZE);
    if (bios_mapped) {
        bios_initialized = 1;
    }
}

int bios_read_byte(uint32_t offset, uint8_t *out) {
    if (!bios_initialized) bios_edit_init();
    if (!bios_mapped || offset >= BIOS_ROM_SIZE || !out) return -1;
    *out = bios_mapped[offset];
    return 0;
}

int bios_write_byte(uint32_t offset, uint8_t value) {
    if (!bios_initialized) bios_edit_init();
    if (!bios_mapped || offset >= BIOS_ROM_SIZE) return -1;
    /* 写入 BIOS ROM 通常不可行（ROM 是只读的），
     * 但某些系统支持通过特定机制写入 RAM 映射区域 */
    bios_mapped[offset] = value;
    return 0;
}

void bios_dump(uint32_t start, uint32_t length) {
    if (!bios_initialized) bios_edit_init();
    if (!bios_mapped) {
        shell_print("bios: BIOS mapping failed\n");
        return;
    }
    if (start >= BIOS_ROM_SIZE) {
        shell_print("bios: Start offset out of range (0-0xFFFF)\n");
        return;
    }
    if (start + length > BIOS_ROM_SIZE) {
        length = BIOS_ROM_SIZE - start;
    }
    if (length == 0) return;

    /* 十六进制转储格式: 偏移量 | 16字节十六进制 | ASCII */
    uint32_t off = start;
    while (off < start + length) {
        char line[128];
        int pos = 0;

        /* 偏移量 */
        pos += snprintf(line + pos, sizeof(line) - pos, "%05X: ", off);

        /* 十六进制值 */
        for (int i = 0; i < 16 && off + i < start + length; i++) {
            pos += snprintf(line + pos, sizeof(line) - pos, "%02X ", bios_mapped[off + i]);
        }
        /* 填充空格 */
        int remaining = 16 - (int)((start + length - off) < 16 ? (start + length - off) : 16);
        for (int i = 0; i < remaining; i++) {
            pos += snprintf(line + pos, sizeof(line) - pos, "   ");
        }

        /* ASCII */
        pos += snprintf(line + pos, sizeof(line) - pos, " |");
        for (int i = 0; i < 16 && off + i < start + length; i++) {
            uint8_t c = bios_mapped[off + i];
            line[pos++] = (c >= 32 && c < 127) ? (char)c : '.';
        }
        line[pos++] = '|';
        line[pos++] = '\n';
        line[pos] = '\0';

        shell_print(line);
        off += 16;
    }
}

/* 确认危险操作 - 用户必须输入 "yes" */
static int bios_confirm(const char *action) {
    shell_print("WARNING: ");
    shell_print(action);
    shell_print(" is a DANGEROUS operation!\n");
    shell_print("Type 'yes' to confirm: ");

    char input[16];
    int len = 0;
    while (len < (int)sizeof(input) - 1) {
        while (!keyboard_has_data()) {
            asm volatile("hlt");
        }
        keyboard_event_t event;
        if (!keyboard_get_event(&event)) continue;
        if (!(event.flags & KEY_PRESSED)) continue;
        if (event.ascii == '\n') {
            shell_print("\n");
            break;
        }
        if (event.ascii == '\b') {
            if (len > 0) { len--; shell_print("\b"); }
            continue;
        }
        if (event.ascii >= 32 && event.ascii < 127) {
            input[len++] = event.ascii;
            shell_print("*");
        }
    }
    input[len] = '\0';
    return strcmp(input, "yes") == 0;
}

int bios_edit_cmd(const char *args) {
    if (!args || !*args) {
        shell_print("Usage: bios <command> [args...]\n");
        shell_print("Commands:\n");
        shell_print("  dump <start> <len>  - Dump BIOS ROM region\n");
        shell_print("  read <offset>       - Read a single byte\n");
        shell_print("  write <offset> <val>- Write a byte (DANGEROUS!)\n");
        shell_print("  info                - Show BIOS info\n");
        return 1;
    }

    if (!bios_initialized) bios_edit_init();
    if (!bios_mapped) {
        shell_print("bios: Failed to map BIOS ROM area\n");
        return 1;
    }

    /* 解析子命令 */
    char cmd[32];
    const char *p = args;
    int ci = 0;
    while (*p && *p != ' ' && ci < 31) {
        cmd[ci++] = *p++;
    }
    cmd[ci] = '\0';
    while (*p == ' ') p++;

    if (strcmp(cmd, "dump") == 0) {
        /* bios dump <start> <len> */
        uint32_t start = 0, length = 256;
        if (*p) {
            start = (uint32_t)strtol(p, (char **)&p, 16);
            while (*p == ' ') p++;
            if (*p) {
                length = (uint32_t)strtol(p, 0, 0);
            }
        }
        if (length == 0) length = 256;
        if (length > 4096) length = 4096;  /* 限制最大转储大小 */

        char buf[64];
        snprintf(buf, sizeof(buf), "Dumping BIOS ROM: 0x%05X - 0x%05X (%u bytes)\n",
                 start, start + length - 1, length);
        shell_print(buf);
        bios_dump(start, length);
        return 0;
    } else if (strcmp(cmd, "read") == 0) {
        /* bios read <offset> */
        if (!*p) {
            shell_print("Usage: bios read <offset>\n");
            return 1;
        }
        uint32_t offset = (uint32_t)strtol(p, 0, 16);
        uint8_t val;
        if (bios_read_byte(offset, &val) == 0) {
            char buf[64];
            snprintf(buf, sizeof(buf), "BIOS[0x%05X] = 0x%02X (%u)\n", offset, val, val);
            shell_print(buf);
            return 0;
        } else {
            shell_print("bios: Read failed (offset out of range)\n");
            return 1;
        }
    } else if (strcmp(cmd, "write") == 0) {
        /* bios write <offset> <value> - 危险操作，需要确认 */
        if (!*p) {
            shell_print("Usage: bios write <offset> <value>\n");
            return 1;
        }
        uint32_t offset = (uint32_t)strtol(p, (char **)&p, 16);
        while (*p == ' ') p++;
        if (!*p) {
            shell_print("bios: Missing value argument\n");
            return 1;
        }
        uint8_t value = (uint8_t)strtol(p, 0, 16);

        /* 安全确认 */
        char warn[128];
        snprintf(warn, sizeof(warn),
                 "Writing 0x%02X to BIOS offset 0x%05X", value, offset);
        if (!bios_confirm(warn)) {
            shell_print("bios: Write cancelled\n");
            return 1;
        }

        if (bios_write_byte(offset, value) == 0) {
            char buf[64];
            snprintf(buf, sizeof(buf), "BIOS[0x%05X] = 0x%02X (written)\n", offset, value);
            shell_print(buf);
            return 0;
        } else {
            shell_print("bios: Write failed (offset out of range or ROM protected)\n");
            return 1;
        }
    } else if (strcmp(cmd, "info") == 0) {
        /* 显示 BIOS 基本信息 */
        shell_print("BIOS ROM Information:\n");
        char buf[128];
        snprintf(buf, sizeof(buf), "  Physical range: 0x%05X - 0x%05X (%u KB)\n",
                 BIOS_ROM_BASE, BIOS_ROM_END, BIOS_ROM_SIZE / 1024);
        shell_print(buf);
        snprintf(buf, sizeof(buf), "  Mapped at: 0x%08X\n", (uint32_t)(uintptr_t)bios_mapped);
        shell_print(buf);

        /* 尝试读取 BIOS 签名 */
        if (bios_mapped) {
            /* 检查 0xFFFF5 处的 BIOS 日期签名 */
            uint8_t date[9];
            int valid = 1;
            for (int i = 0; i < 8; i++) {
                if (bios_read_byte(0xFFF5 - BIOS_ROM_BASE + i, &date[i]) != 0) {
                    valid = 0;
                    break;
                }
            }
            if (valid && date[2] == '/' && date[5] == '/') {
                date[8] = '\0';
                shell_print("  BIOS Date: ");
                shell_print((const char *)date);
                shell_print("\n");
            }

            /* 检查 0xFFFE0 处的 BIOS 签名 */
            uint8_t sig[9] = {0};
            for (int i = 0; i < 8; i++) {
                bios_read_byte(0xFFFE0 - BIOS_ROM_BASE + i, &sig[i]);
            }
            shell_print("  BIOS Signature: ");
            shell_print((const char *)sig);
            shell_print("\n");
        }
        return 0;
    } else {
        shell_print("bios: Unknown command '");
        shell_print(cmd);
        shell_print("'\n");
        shell_print("Use: bios dump|read|write|info\n");
        return 1;
    }
}
