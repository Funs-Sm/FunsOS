#ifndef APP_UTILS_H
#define APP_UTILS_H

void calc_interactive_run(void);
void sysmon_run(void);
void cal_run(void);
void clock_run(void);
void matrix_run(void);

void life_run(void);
void sokoban_run(void);
void typing_run(void);
void ascii_table_run(void);
void cowsay_run(const char *msg);
void fortune_run(void);
void primes_run(const char *limit_str);
void banner_run(const char *text);
void mktemp_run(void);
void rain_run(void);
void snow_run(void);
void fire_run(void);
void rot13_run(const char *arg);
void tty_run(void);

int bios_edit_cmd(const char *path);

/* These take const char *args - see more_apps.h */
void number_run(const char *num_str);
void wc_run(const char *filepath);
void file_run(const char *filepath);
void head_run(const char *filepath, const char *lines);
void tail_run(const char *filepath, const char *lines);
void plasma_run(void);
void reset_run(void);

void shell_err_unknown(const char *name);

#endif
