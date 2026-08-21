/*
 * flock_demo.c - 文件锁演示程序
 *
 * 演示如何使用 BSD flock 文件锁机制来协调多进程对共享文件的访问。
 *
 * 使用方法:
 *   ./flock_demo            - 作为锁持有者运行
 *   ./flock_demo release    - 释放锁
 */

#include "user_syscall.h"
#include "string.h"

#define LOCK_FILE "/tmp/shared_lock.txt"
#define BUFFER_SIZE 256

static void print_status(const char *msg) {
    sys_write(1, msg, strlen(msg));
    sys_write(1, "\n", 1);
}

static void print_num(const char *label, int val) {
    sys_write(1, label, strlen(label));

    char buf[32];
    int i = 0;
    if (val < 0) {
        buf[i++] = '-';
        val = -val;
    }
    if (val == 0) {
        buf[i++] = '0';
    } else {
        char tmp[16];
        int j = 0;
        while (val > 0) {
            tmp[j++] = '0' + (val % 10);
            val /= 10;
        }
        while (j > 0) {
            buf[i++] = tmp[--j];
        }
    }
    buf[i] = '\0';
    sys_write(1, buf, i);
    sys_write(1, "\n", 1);
}

int main(int argc, char *argv[]) {
    int fd;
    int ret;

    print_status("=== File Lock (flock) Demo ===");

    /* 解析命令行参数 */
    int release_mode = 0;
    if (argc > 1 && argv[1][0] == 'r') {
        release_mode = 1;
        print_status("Mode: Release lock");
    } else {
        print_status("Mode: Acquire lock");
    }

    /* 打开或创建锁文件 */
    fd = sys_open(LOCK_FILE, O_CREAT | O_RDWR);
    if (fd < 0) {
        print_status("Error: Cannot open lock file");
        return 1;
    }

    if (release_mode) {
        /* 释放锁 */
        ret = sys_flock(fd, LOCK_UN);
        if (ret == 0) {
            print_status("Lock released successfully");
        } else {
            print_status("Error: Failed to release lock");
        }
    } else {
        /* 尝试获取排他锁 (阻塞模式) */
        print_status("Attempting to acquire exclusive lock...");
        ret = sys_flock(fd, LOCK_EX);
        if (ret == 0) {
            print_status("Lock acquired!");

            /* 模拟临界区操作 */
            print_status("Holding lock for 5 seconds...");
            print_status("(Other processes trying to acquire lock will block)");

            /* 写入一些数据到锁文件 */
            const char *data = "Locked by process - critical section\n";
            sys_write(fd, data, strlen(data));

            /* 等待一段时间 */
            sys_sleep(5);

            /* 释放锁 */
            ret = sys_flock(fd, LOCK_UN);
            if (ret == 0) {
                print_status("Lock released");
            } else {
                print_status("Error: Failed to release lock");
            }
        } else {
            print_status("Error: Failed to acquire lock");
        }
    }

    sys_close(fd);
    print_status("Done");
    return 0;
}
