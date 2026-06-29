#include "vbox_rng.h"
#include "klog.h"
#include "timer.h"
#include "devfs.h"
#include "string.h"

#define VBOX_RNG_A 6364136223846793005ULL
#define VBOX_RNG_C 1442695040888963407ULL

static uint64_t vbox_rng_state = 0;
static int vbox_rng_initialized = 0;

static void vbox_rng_next_state(void) {
    vbox_rng_state = VBOX_RNG_A * vbox_rng_state + VBOX_RNG_C;
}

static uint32_t vbox_rng_next_u32(void) {
    uint64_t x;

    vbox_rng_next_state();
    x = vbox_rng_state;
    x ^= (x >> 33);
    x *= 0xff51afd7ed558ccdULL;
    x ^= (x >> 33);
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= (x >> 33);

    return (uint32_t)(x >> 32);
}

void vbox_rng_seed(uint64_t seed) {
    vbox_rng_state = seed;
    vbox_rng_initialized = 1;
    klog_info("vbox_rng: seeded with 0x%llx", (unsigned long long)seed);
}

uint32_t vbox_rng_read(uint8_t *buf, uint32_t len) {
    uint32_t filled = 0;

    if (!buf || len == 0) return 0;

    if (!vbox_rng_initialized) {
        vbox_rng_seed(timer_get_ticks() ^ 0xDEADBEEFCAFEBABEULL);
    }

    while (filled + 4 <= len) {
        uint32_t val = vbox_rng_next_u32();
        buf[filled++] = (uint8_t)(val & 0xFF);
        buf[filled++] = (uint8_t)((val >> 8) & 0xFF);
        buf[filled++] = (uint8_t)((val >> 16) & 0xFF);
        buf[filled++] = (uint8_t)((val >> 24) & 0xFF);
    }

    if (filled < len) {
        uint32_t val = vbox_rng_next_u32();
        while (filled < len) {
            buf[filled++] = (uint8_t)(val & 0xFF);
            val >>= 8;
        }
    }

    return len;
}

uint32_t vbox_rng_u32(void) {
    uint32_t val = 0;
    vbox_rng_read((uint8_t *)&val, 4);
    return val;
}

uint64_t vbox_rng_u64(void) {
    uint64_t val = 0;
    vbox_rng_read((uint8_t *)&val, 8);
    return val;
}

uint8_t vbox_rng_byte(void) {
    uint8_t val = 0;
    vbox_rng_read(&val, 1);
    return val;
}

static int32_t vbox_rng_dev_read(file_t *file, void *buf, uint32_t count) {
    (void)file;
    return (int32_t)vbox_rng_read((uint8_t *)buf, count);
}

static int32_t vbox_rng_dev_write(file_t *file, const void *buf, uint32_t count) {
    (void)file;
    if (!buf || count == 0) return 0;
    if (count >= 8) {
        uint64_t seed = 0;
        memcpy(&seed, buf, 8);
        vbox_rng_seed(seed);
    }
    return (int32_t)count;
}

static int32_t vbox_rng_dev_open(inode_t *inode, file_t *file) {
    (void)inode;
    (void)file;
    return 0;
}

static int32_t vbox_rng_dev_close(file_t *file) {
    (void)file;
    return 0;
}

static file_ops_t vbox_random_ops = {
    .open = vbox_rng_dev_open,
    .read = vbox_rng_dev_read,
    .write = vbox_rng_dev_write,
    .close = vbox_rng_dev_close,
    .seek = NULL,
    .ioctl = NULL
};

static file_ops_t vbox_urandom_ops = {
    .open = vbox_rng_dev_open,
    .read = vbox_rng_dev_read,
    .write = vbox_rng_dev_write,
    .close = vbox_rng_dev_close,
    .seek = NULL,
    .ioctl = NULL
};

int vbox_rng_init(void) {
    uint64_t initial_seed;

    initial_seed = timer_get_ticks();
    initial_seed ^= ((uint64_t)timer_get_ticks() << 32);
    initial_seed ^= 0xDEADBEEFCAFEBABEULL;

    vbox_rng_seed(initial_seed);

    if (devfs_register("random", DEVICE_CHAR, VBOX_RNG_MAJOR,
                       VBOX_RNG_MINOR_RANDOM, &vbox_random_ops, NULL) != 0) {
        klog_warn("vbox_rng: failed to register /dev/random");
    } else {
        klog_info("vbox_rng: registered /dev/random");
    }

    if (devfs_register("urandom", DEVICE_CHAR, VBOX_RNG_MAJOR,
                       VBOX_RNG_MINOR_URANDOM, &vbox_urandom_ops, NULL) != 0) {
        klog_warn("vbox_rng: failed to register /dev/urandom");
    } else {
        klog_info("vbox_rng: registered /dev/urandom");
    }

    klog_info("vbox_rng: initialized (LCG-based PRNG)");
    return 0;
}
