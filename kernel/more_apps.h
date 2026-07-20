#ifndef MORE_APPS_H
#define MORE_APPS_H

void life_run(void);
void sokoban_run(void);
void typing_run(void);
void ascii_table_run(void);
void hexdump_run(const char *filepath);
void dmesg_run(void);
void uname_run(void);
void nano_run(const char *filepath);
void top_run(void);

void file_run(const char *filepath);
void strings_run(const char *filepath);
void stat_run(const char *filepath);
void factor_run(const char *num_str);
void primes_run(const char *limit_str);
void yes_run(const char *str);
void cowsay_run(const char *msg);
void fortune_run(void);
void sleep_run(const char *sec_str);
void seq_run(const char *a, const char *b, const char *c);
void banner_run(const char *text);
void mktemp_run(void);
void cp_run(const char *src, const char *dst);

void rain_run(void);
void snow_run(void);
void fire_run(void);
void matrix2_run(void);
void rot13_run(const char *text);
void rev_run(const char *text);
void whoami_run(void);
void tty_run(void);
void number_run(const char *num_str);
void cksum_run(const char *filepath);
void reset_run(void);

void wc_run(const char *filepath);
void head_run(const char *filepath, const char *lines);
void tail_run(const char *filepath, const char *lines);
void plasma_run(void);

#endif
