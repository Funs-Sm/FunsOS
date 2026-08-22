/* cmd_all.c - Fallback stub implementations for un-migrated commands.
 * Real implementations in dedicated cmd_*.c or other kernel modules
 * (e.g. fw_cmd.c) take precedence at link time.
 */
#include "cmd_all.h"
#include <stddef.h>

/* Provided by shell.c */
extern void shell_print(const char *str);
extern int shell_last_exit_code;

int cmd_life(int argc, char *argv[]) { (void)0; shell_print("cmd_life: stub\n"); shell_last_exit_code = 0; return 0; }
int cmd_sokoban(int argc, char *argv[]) { (void)0; shell_print("cmd_sokoban: stub\n"); shell_last_exit_code = 0; return 0; }
int cmd_typing(int argc, char *argv[]) { (void)0; shell_print("cmd_typing: stub\n"); shell_last_exit_code = 0; return 0; }
int cmd_ascii(int argc, char *argv[]) { (void)0; shell_print("cmd_ascii: stub\n"); shell_last_exit_code = 0; return 0; }
int cmd_nano(int argc, char *argv[]) { (void)0; shell_print("cmd_nano: stub\n"); shell_last_exit_code = 0; return 0; }
/* cmd_edit moved to kernel/cmd_util2.c */
/* cmd_pt moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_show moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_go moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_where moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_clr moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_ver moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_help moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_schedpolicy moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_mempolicy moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_reboot / cmd_halt / cmd_shutdown / cmd_sleep / cmd_watch /
 * cmd_time / cmd_time_cmd moved to kernel/cmd_power.c and kernel/cmd_time.c */
/* cmd_mem moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_dev moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_copy moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_del moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_mkdir moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_ren moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_type / cmd_which moved to kernel/cmd_utility.c */
/* cmd_find moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_size moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_echo moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_set moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_unset moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_run moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_load moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_append moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_setenv moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_unsetenv moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_bg / cmd_fg / cmd_jobs / cmd_nice / cmd_renice / cmd_nohup moved to kernel/cmd_procctl.c */
/* cmd_watch / cmd_sleep moved to kernel/cmd_time.c */
/* cmd_xargs / cmd_tee / cmd_install / cmd_which moved to kernel/cmd_utility.c */
/* cmd_logrotate moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_logrotate_ext moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_imgview moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_vol moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_sound moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_guistop moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_crepl moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_exec moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_gui moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_run_app moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_taskbar moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_search moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_fc moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_save moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_resume moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_logout moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_test / cmd_expr moved to kernel/cmd_utility.c */
/* cmd_pidof moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_pstree moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_last moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_taskset moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_chrt moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_strace moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_lsof moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_prlimit moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_capsh moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_sysreport moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_dumpstack moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_kwork moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_ktrace moved to kernel/cmd_kdebug.c */
/* cmd_kprobe moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_notifier moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_sysctl moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_losetup moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_fallocate moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_filefrag moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_fsck moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_fsck_ext moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_sensors moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_cpufreq moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_i2c moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_spi moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_gpio moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_rtc moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_pinctrl moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_clk moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_dmaengine moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_mfd moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_devtmpfs moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_sysfs moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_netns moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_netfilter moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_seccomp moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_apparmor moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_keyring moved to a dedicated cmd_*.c module (see CHANGELOG) */
/* cmd_audit moved to a dedicated cmd_*.c module (see CHANGELOG) */
void cmd_watchdog(const char *arg) { (void)arg; shell_print("cmd_watchdog: stub\n"); shell_last_exit_code = 0; }
void cmd_applist(const char *arg) { (void)arg; shell_print("cmd_applist: stub\n"); shell_last_exit_code = 0; }
void cmd_cpuidle(const char *arg) { (void)arg; shell_print("cmd_cpuidle: stub\n"); shell_last_exit_code = 0; }
void cmd_crypto(const char *arg) { (void)arg; shell_print("cmd_crypto: stub\n"); shell_last_exit_code = 0; }
void cmd_firmware(const char *arg) { (void)arg; shell_print("cmd_firmware: stub\n"); shell_last_exit_code = 0; }
void cmd_remoteproc(const char *arg) { (void)arg; shell_print("cmd_remoteproc: stub\n"); shell_last_exit_code = 0; }
void cmd_rpmsg(const char *arg) { (void)arg; shell_print("cmd_rpmsg: stub\n"); shell_last_exit_code = 0; }
void cmd_virtio(const char *arg) { (void)arg; shell_print("cmd_virtio: stub\n"); shell_last_exit_code = 0; }
void cmd_vmalloc(const char *arg) { (void)arg; shell_print("cmd_vmalloc: stub\n"); shell_last_exit_code = 0; }
void cmd_percpu(const char *arg) { (void)arg; shell_print("cmd_percpu: stub\n"); shell_last_exit_code = 0; }
void cmd_kfence(const char *arg) { (void)arg; shell_print("cmd_kfence: stub\n"); shell_last_exit_code = 0; }
void cmd_debugobj(const char *arg) { (void)arg; shell_print("cmd_debugobj: stub\n"); shell_last_exit_code = 0; }
void cmd_lockdep(const char *arg) { (void)arg; shell_print("cmd_lockdep: stub\n"); shell_last_exit_code = 0; }
void cmd_irqdomain(const char *arg) { (void)arg; shell_print("cmd_irqdomain: stub\n"); shell_last_exit_code = 0; }
void cmd_regmap(const char *arg) { (void)arg; shell_print("cmd_regmap: stub\n"); shell_last_exit_code = 0; }
void cmd_hwmon(const char *arg) { (void)arg; shell_print("cmd_hwmon: stub\n"); shell_last_exit_code = 0; }
void cmd_ftrace(const char *arg) { (void)arg; shell_print("cmd_ftrace: stub\n"); shell_last_exit_code = 0; }
void cmd_dmabuf(const char *arg) { (void)arg; shell_print("cmd_dmabuf: stub\n"); shell_last_exit_code = 0; }
void cmd_namespace(const char *arg) { (void)arg; shell_print("cmd_namespace: stub\n"); shell_last_exit_code = 0; }
/* cmd_tracepoint moved to kernel/cmd_kdebug.c */
void cmd_uprobe(const char *arg) { (void)arg; shell_print("cmd_uprobe: stub\n"); shell_last_exit_code = 0; }
void cmd_kmod(const char *arg) { (void)arg; shell_print("cmd_kmod: stub\n"); shell_last_exit_code = 0; }
void cmd_iio(const char *arg) { (void)arg; shell_print("cmd_iio: stub\n"); shell_last_exit_code = 0; }
void cmd_pwm(const char *arg) { (void)arg; shell_print("cmd_pwm: stub\n"); shell_last_exit_code = 0; }
void cmd_led(const char *arg) { (void)arg; shell_print("cmd_led: stub\n"); shell_last_exit_code = 0; }
/* cmd_alias moved to a dedicated cmd_*.c module (see CHANGELOG) */
void cmd_apps(const char *arg) { (void)arg; shell_print("cmd_apps: stub\n"); shell_last_exit_code = 0; }
void cmd_base64(const char *arg) { (void)arg; shell_print("cmd_base64: stub\n"); shell_last_exit_code = 0; }
void cmd_calc(const char *arg) { (void)arg; shell_print("cmd_calc: stub\n"); shell_last_exit_code = 0; }
void cmd_cat(const char *arg) { (void)arg; shell_print("cmd_cat: stub\n"); shell_last_exit_code = 0; }
void cmd_cd(const char *arg) { (void)arg; shell_print("cmd_cd: stub\n"); shell_last_exit_code = 0; }
void cmd_cedit(const char *arg) { (void)arg; shell_print("cmd_cedit: stub\n"); shell_last_exit_code = 0; }
void cmd_cgroup(const char *arg) { (void)arg; shell_print("cmd_cgroup: stub\n"); shell_last_exit_code = 0; }
void cmd_chkdsk(const char *arg) { (void)arg; shell_print("cmd_chkdsk: stub\n"); shell_last_exit_code = 0; }
void cmd_chmod(const char *arg) { (void)arg; shell_print("cmd_chmod: stub\n"); shell_last_exit_code = 0; }
void cmd_chown(const char *arg) { (void)arg; shell_print("cmd_chown: stub\n"); shell_last_exit_code = 0; }
void cmd_col(const char *arg) { (void)arg; shell_print("cmd_col: stub\n"); shell_last_exit_code = 0; }
void cmd_column(const char *arg) { (void)arg; shell_print("cmd_column: stub\n"); shell_last_exit_code = 0; }
void cmd_compress(const char *arg) { (void)arg; shell_print("cmd_compress: stub\n"); shell_last_exit_code = 0; }
void cmd_config(const char *arg) { (void)arg; shell_print("cmd_config: stub\n"); shell_last_exit_code = 0; }
void cmd_db(const char *arg) { (void)arg; shell_print("cmd_db: stub\n"); shell_last_exit_code = 0; }
void cmd_dcache(const char *arg) { (void)arg; shell_print("cmd_dcache: stub\n"); shell_last_exit_code = 0; }
void cmd_dd_full(const char *arg) { (void)arg; shell_print("cmd_dd_full: stub\n"); shell_last_exit_code = 0; }
void cmd_decompress(const char *arg) { (void)arg; shell_print("cmd_decompress: stub\n"); shell_last_exit_code = 0; }
void cmd_diff(const char *arg) { (void)arg; shell_print("cmd_diff: stub\n"); shell_last_exit_code = 0; }
void cmd_du(const char *arg) { (void)arg; shell_print("cmd_du: stub\n"); shell_last_exit_code = 0; }
/* cmd_env moved to a dedicated cmd_*.c module (see CHANGELOG) */
void cmd_epollinfo(const char *arg) { (void)arg; shell_print("cmd_epollinfo: stub\n"); shell_last_exit_code = 0; }
void cmd_evlog(const char *arg) { (void)arg; shell_print("cmd_evlog: stub\n"); shell_last_exit_code = 0; }
void cmd_fdisk(const char *arg) { (void)arg; shell_print("cmd_fdisk: stub\n"); shell_last_exit_code = 0; }
void cmd_file(const char *arg) { (void)arg; shell_print("cmd_file: stub\n"); shell_last_exit_code = 0; }
void cmd_fim(const char *arg) { (void)arg; shell_print("cmd_fim: stub\n"); shell_last_exit_code = 0; }
void cmd_flock(const char *arg) { (void)arg; shell_print("cmd_flock: stub\n"); shell_last_exit_code = 0; }
void cmd_flock_cmd(const char *arg) { (void)arg; shell_print("cmd_flock_cmd: stub\n"); shell_last_exit_code = 0; }
void cmd_fmt(const char *arg) { (void)arg; shell_print("cmd_fmt: stub\n"); shell_last_exit_code = 0; }
void cmd_format(const char *arg) { (void)arg; shell_print("cmd_format: stub\n"); shell_last_exit_code = 0; }
void cmd_freq(const char *arg) { (void)arg; shell_print("cmd_freq: stub\n"); shell_last_exit_code = 0; }
void cmd_fsstat(const char *arg) { (void)arg; shell_print("cmd_fsstat: stub\n"); shell_last_exit_code = 0; }
void cmd_futexinfo(const char *arg) { (void)arg; shell_print("cmd_futexinfo: stub\n"); shell_last_exit_code = 0; }
void cmd_grep(const char *arg) { (void)arg; shell_print("cmd_grep: stub\n"); shell_last_exit_code = 0; }
void cmd_head(const char *arg) { (void)arg; shell_print("cmd_head: stub\n"); shell_last_exit_code = 0; }
void cmd_health(const char *arg) { (void)arg; shell_print("cmd_health: stub\n"); shell_last_exit_code = 0; }
/* cmd_history moved to a dedicated cmd_*.c module (see CHANGELOG) */
void cmd_hrtimer(const char *arg) { (void)arg; shell_print("cmd_hrtimer: stub\n"); shell_last_exit_code = 0; }
void cmd_icache(const char *arg) { (void)arg; shell_print("cmd_icache: stub\n"); shell_last_exit_code = 0; }
void cmd_inotifyinfo(const char *arg) { (void)arg; shell_print("cmd_inotifyinfo: stub\n"); shell_last_exit_code = 0; }
void cmd_iosched(const char *arg) { (void)arg; shell_print("cmd_iosched: stub\n"); shell_last_exit_code = 0; }
void cmd_ipc(const char *arg) { (void)arg; shell_print("cmd_ipc: stub\n"); shell_last_exit_code = 0; }
void cmd_kvm(const char *arg) { (void)arg; shell_print("cmd_kvm: stub\n"); shell_last_exit_code = 0; }
void cmd_ln(const char *arg) { (void)arg; shell_print("cmd_ln: stub\n"); shell_last_exit_code = 0; }
void cmd_login(const char *arg) { (void)arg; shell_print("cmd_login: stub\n"); shell_last_exit_code = 0; }
void cmd_ls(const char *arg) { (void)arg; shell_print("cmd_ls: stub\n"); shell_last_exit_code = 0; }
void cmd_lsblk(const char *arg) { (void)arg; shell_print("cmd_lsblk: stub\n"); shell_last_exit_code = 0; }
void cmd_lscolor(const char *arg) { (void)arg; shell_print("cmd_lscolor: stub\n"); shell_last_exit_code = 0; }
void cmd_lspci(const char *arg) { (void)arg; shell_print("cmd_lspci: stub\n"); shell_last_exit_code = 0; }
void cmd_lsusb(const char *arg) { (void)arg; shell_print("cmd_lsusb: stub\n"); shell_last_exit_code = 0; }
void cmd_md5(const char *arg) { (void)arg; shell_print("cmd_md5: stub\n"); shell_last_exit_code = 0; }
void cmd_mkfifo(const char *arg) { (void)arg; shell_print("cmd_mkfifo: stub\n"); shell_last_exit_code = 0; }
void cmd_mknod(const char *arg) { (void)arg; shell_print("cmd_mknod: stub\n"); shell_last_exit_code = 0; }
void cmd_mount(const char *arg) { (void)arg; shell_print("cmd_mount: stub\n"); shell_last_exit_code = 0; }
void cmd_mount2(const char *arg) { (void)arg; shell_print("cmd_mount2: stub\n"); shell_last_exit_code = 0; }
void cmd_ntp(const char *arg) { (void)arg; shell_print("cmd_ntp: stub\n"); shell_last_exit_code = 0; }
void cmd_oom(const char *arg) { (void)arg; shell_print("cmd_oom: stub\n"); shell_last_exit_code = 0; }
void cmd_pagecache(const char *arg) { (void)arg; shell_print("cmd_pagecache: stub\n"); shell_last_exit_code = 0; }
void cmd_passwd(const char *arg) { (void)arg; shell_print("cmd_passwd: stub\n"); shell_last_exit_code = 0; }
void cmd_pkg(const char *arg) { (void)arg; shell_print("cmd_pkg: stub\n"); shell_last_exit_code = 0; }
void cmd_play(const char *arg) { (void)arg; shell_print("cmd_play: stub\n"); shell_last_exit_code = 0; }
void cmd_quota(const char *arg) { (void)arg; shell_print("cmd_quota: stub\n"); shell_last_exit_code = 0; }
void cmd_quota_ext(const char *arg) { (void)arg; shell_print("cmd_quota_ext: stub\n"); shell_last_exit_code = 0; }
void cmd_rcu(const char *arg) { (void)arg; shell_print("cmd_rcu: stub\n"); shell_last_exit_code = 0; }
void cmd_readahead_stat(const char *arg) { (void)arg; shell_print("cmd_readahead_stat: stub\n"); shell_last_exit_code = 0; }
void cmd_readlink(const char *arg) { (void)arg; shell_print("cmd_readlink: stub\n"); shell_last_exit_code = 0; }
void cmd_reg(const char *arg) { (void)arg; shell_print("cmd_reg: stub\n"); shell_last_exit_code = 0; }
void cmd_replace(const char *arg) { (void)arg; shell_print("cmd_replace: stub\n"); shell_last_exit_code = 0; }
void cmd_slab(const char *arg) { (void)arg; shell_print("cmd_slab: stub\n"); shell_last_exit_code = 0; }
void cmd_snapshot(const char *arg) { (void)arg; shell_print("cmd_snapshot: stub\n"); shell_last_exit_code = 0; }
void cmd_softirq(const char *arg) { (void)arg; shell_print("cmd_softirq: stub\n"); shell_last_exit_code = 0; }
void cmd_sort(const char *arg) { (void)arg; shell_print("cmd_sort: stub\n"); shell_last_exit_code = 0; }
void cmd_stat(const char *arg) { (void)arg; shell_print("cmd_stat: stub\n"); shell_last_exit_code = 0; }
void cmd_su(const char *arg) { (void)arg; shell_print("cmd_su: stub\n"); shell_last_exit_code = 0; }
void cmd_sudo(const char *arg) { (void)arg; shell_print("cmd_sudo: stub\n"); shell_last_exit_code = 0; }
void cmd_syncstat(const char *arg) { (void)arg; shell_print("cmd_syncstat: stub\n"); shell_last_exit_code = 0; }
void cmd_sysacct(const char *arg) { (void)arg; shell_print("cmd_sysacct: stub\n"); shell_last_exit_code = 0; }
void cmd_sysrq(const char *arg) { (void)arg; shell_print("cmd_sysrq: stub\n"); shell_last_exit_code = 0; }
void cmd_tail(const char *arg) { (void)arg; shell_print("cmd_tail: stub\n"); shell_last_exit_code = 0; }
void cmd_taskmgr(const char *arg) { (void)arg; shell_print("cmd_taskmgr: stub\n"); shell_last_exit_code = 0; }
void cmd_tftp(const char *arg) { (void)arg; shell_print("cmd_tftp: stub\n"); shell_last_exit_code = 0; }
void cmd_touch(const char *arg) { (void)arg; shell_print("cmd_touch: stub\n"); shell_last_exit_code = 0; }
void cmd_tree(const char *arg) { (void)arg; shell_print("cmd_tree: stub\n"); shell_last_exit_code = 0; }
void cmd_ulimit(const char *arg) { (void)arg; shell_print("cmd_ulimit: stub\n"); shell_last_exit_code = 0; }
void cmd_umount(const char *arg) { (void)arg; shell_print("cmd_umount: stub\n"); shell_last_exit_code = 0; }
void cmd_uniq(const char *arg) { (void)arg; shell_print("cmd_uniq: stub\n"); shell_last_exit_code = 0; }
void cmd_useradd(const char *arg) { (void)arg; shell_print("cmd_useradd: stub\n"); shell_last_exit_code = 0; }
void cmd_userdel(const char *arg) { (void)arg; shell_print("cmd_userdel: stub\n"); shell_last_exit_code = 0; }
void cmd_version(const char *arg) { (void)arg; shell_print("cmd_version: stub\n"); shell_last_exit_code = 0; }
void cmd_watch_dir(const char *arg) { (void)arg; shell_print("cmd_watch_dir: stub\n"); shell_last_exit_code = 0; }
void cmd_wc(const char *arg) { (void)arg; shell_print("cmd_wc: stub\n"); shell_last_exit_code = 0; }
void cmd_workqueue(const char *arg) { (void)arg; shell_print("cmd_workqueue: stub\n"); shell_last_exit_code = 0; }
void cmd_xattr(const char *arg) { (void)arg; shell_print("cmd_xattr: stub\n"); shell_last_exit_code = 0; }
void cmd_df(void) { shell_print("cmd_df: stub\n"); shell_last_exit_code = 0; }
void cmd_httpget(const char *a, const char *b) { (void)a, (void)b; shell_print("cmd_httpget: stub\n"); shell_last_exit_code = 0; }
void cmd_ipcs(void) { shell_print("cmd_ipcs: stub\n"); shell_last_exit_code = 0; }
void cmd_netmon(void) { shell_print("cmd_netmon: stub\n"); shell_last_exit_code = 0; }
void cmd_pwd(void) { shell_print("cmd_pwd: stub\n"); shell_last_exit_code = 0; }
void cmd_sigstat(void) { shell_print("cmd_sigstat: stub\n"); shell_last_exit_code = 0; }
void cmd_slabtop(void) { shell_print("cmd_slabtop: stub\n"); shell_last_exit_code = 0; }
void cmd_ss(const char *arg) { (void)arg; shell_print("cmd_ss: stub\n"); shell_last_exit_code = 0; }
void cmd_sync(void) { shell_print("cmd_sync: stub\n"); shell_last_exit_code = 0; }
