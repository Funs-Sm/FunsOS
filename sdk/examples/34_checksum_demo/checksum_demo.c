/* checksum_demo.c - 校验和示例
 * 演示 FUNSOS SDK v1.5.0 校验和计算功能。
 */

#include "funsos.h"
#include "funsos_checksum.h"

#define TEST_DATA_SIZE  1024

static uint8_t g_test_data[TEST_DATA_SIZE];

int main(void)
{
    funsos_window_t win = funsos_create_window(100, 80, 650, 550, "校验和示例 v1.5.0");
    funsos_fill_window(win, 0xFFFFFF);

    funsos_color_t black = {0x00, 0x00, 0x00, 0xFF};
    funsos_color_t blue  = {0x00, 0x00, 0xFF, 0xFF};
    funsos_color_t green = {0x00, 0x80, 0x00, 0xFF};
    funsos_color_t red   = {0xFF, 0x00, 0x00, 0xFF};
    funsos_color_t purple = {0x80, 0x00, 0x80, 0xFF};
    funsos_color_t orange = {0xFF, 0x80, 0x00, 0xFF};

    funsos_draw_text(win, 20, 20, "FUNSOS SDK v1.5.0 - 校验和示例", blue);
    funsos_draw_text(win, 20, 45, "CRC32 / MD5 / SHA1 / SHA256 / Adler32", black);

    /* 生成测试数据 */
    for (int i = 0; i < TEST_DATA_SIZE; i++) {
        g_test_data[i] = (uint8_t)(i * 7 + 13);
    }

    char buf[128];

    /* CRC32 */
    funsos_draw_text(win, 20, 80, "CRC32:", purple);
    uint32_t crc = funsos_crc32_calc(g_test_data, TEST_DATA_SIZE);
    funsos_snprintf(buf, sizeof(buf), "  值: 0x%08X", crc);
    funsos_draw_text(win, 40, 105, buf, black);
    funsos_snprintf(buf, sizeof(buf), "  数据大小: %d 字节", TEST_DATA_SIZE);
    funsos_draw_text(win, 40, 125, buf, black);

    /* CRC16 */
    funsos_draw_text(win, 20, 155, "CRC16:", purple);
    uint16_t crc16 = funsos_crc16_calc(g_test_data, TEST_DATA_SIZE);
    funsos_snprintf(buf, sizeof(buf), "  值: 0x%04X", crc16);
    funsos_draw_text(win, 40, 180, buf, black);

    /* Adler-32 */
    funsos_draw_text(win, 20, 210, "Adler-32:", purple);
    uint32_t adler = funsos_adler32(1, g_test_data, TEST_DATA_SIZE);
    funsos_snprintf(buf, sizeof(buf), "  值: 0x%08X", adler);
    funsos_draw_text(win, 40, 235, buf, black);

    /* MD5 */
    funsos_draw_text(win, 20, 265, "MD5:", purple);
    uint8_t md5_digest[FUNSOS_MD5_DIGEST_LENGTH];
    char md5_hex[33];
    funsos_md5_calc(g_test_data, TEST_DATA_SIZE, md5_digest);
    funsos_md5_hex(md5_digest, md5_hex);
    funsos_snprintf(buf, sizeof(buf), "  %s", md5_hex);
    funsos_draw_text(win, 40, 290, buf, green);

    /* SHA1 */
    funsos_draw_text(win, 20, 320, "SHA-1:", purple);
    uint8_t sha1_digest[FUNSOS_SHA1_DIGEST_LENGTH];
    char sha1_hex[41];
    funsos_sha1_calc(g_test_data, TEST_DATA_SIZE, sha1_digest);
    funsos_sha1_hex(sha1_digest, sha1_hex);
    funsos_snprintf(buf, sizeof(buf), "  %s", sha1_hex);
    funsos_draw_text(win, 40, 345, buf, green);

    /* SHA256 */
    funsos_draw_text(win, 20, 375, "SHA-256:", purple);
    uint8_t sha256_digest[FUNSOS_SHA256_DIGEST_LENGTH];
    char sha256_hex[65];
    funsos_sha256_calc(g_test_data, TEST_DATA_SIZE, sha256_digest);
    funsos_sha256_hex(sha256_digest, sha256_hex);
    funsos_snprintf(buf, sizeof(buf), "  %.32s...", sha256_hex);
    funsos_draw_text(win, 40, 400, buf, green);

    /* API 列表 */
    funsos_draw_text(win, 350, 80, "校验和 API:", blue);
    funsos_draw_text(win, 370, 105, "循环冗余校验:", orange);
    funsos_draw_text(win, 390, 125, "crc16_init/update/final/calc", black);
    funsos_draw_text(win, 390, 145, "crc32_init/update/final/calc", black);

    funsos_draw_text(win, 370, 175, "哈希算法:", orange);
    funsos_draw_text(win, 390, 195, "md5_init/update/final/calc", black);
    funsos_draw_text(win, 390, 215, "sha1_init/update/final/calc", black);
    funsos_draw_text(win, 390, 235, "sha256_init/update/final/calc", black);

    funsos_draw_text(win, 370, 265, "其他:", orange);
    funsos_draw_text(win, 390, 285, "adler32()      - Adler-32", black);
    funsos_draw_text(win, 390, 305, "xor_checksum() - XOR 校验", black);

    funsos_draw_text(win, 370, 335, "文件校验:", orange);
    funsos_draw_text(win, 390, 355, "md5_file()    - 文件 MD5", black);
    funsos_draw_text(win, 390, 375, "sha1_file()   - 文件 SHA1", black);
    funsos_draw_text(win, 390, 395, "sha256_file() - 文件 SHA256", black);

    funsos_draw_text(win, 370, 425, "工具函数:", orange);
    funsos_draw_text(win, 390, 445, "md5_hex()    - MD5 转十六进制", black);
    funsos_draw_text(win, 390, 465, "sha1_hex()   - SHA1 转十六进制", black);
    funsos_draw_text(win, 390, 485, "sha256_hex() - SHA256 转十六进制", black);

    /* 流式计算示例 */
    funsos_draw_text(win, 20, 440, "流式计算示例:", blue);
    funsos_draw_text(win, 40, 465, "crc32_init(&ctx);", dark_gray);
    funsos_draw_text(win, 40, 485, "crc32_update(&ctx, data1, len1);", dark_gray);
    funsos_draw_text(win, 40, 505, "crc32_update(&ctx, data2, len2);", dark_gray);
    funsos_draw_text(win, 40, 525, "uint32_t result = crc32_final(&ctx);", dark_gray);

    funsos_draw_text(win, 20, 540, "按 ESC 退出", black);

    /* 事件循环 */
    funsos_event_t event;
    while (1) {
        if (funsos_wait_event(&event) != 0)
            continue;
        if (event.type == FUNSOS_EVENT_KEY_PRESS && event.key == 0x1B)
            break;
    }

    funsos_destroy_window(win);
    return 0;
}
