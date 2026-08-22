#ifndef _KERNEL_CMD_ALL_H

#define _KERNEL_CMD_ALL_H


/* Auto-generated - ALL cmd_* declarations needed by shell.c

 * Some overlap with other module headers; this header ensures

 * every cmd_* call in shell.c has a declaration.

 */


/* === Games / Apps (main argc, argv style) === */

extern int cmd_life(int argc, char *argv[]);

extern int cmd_sokoban(int argc, char *argv[]);

extern int cmd_typing(int argc, char *argv[]);

extern int cmd_ascii(int argc, char *argv[]);

extern int cmd_nano(int argc, char *argv[]);


/* === All remaining shell commands (const char *arg style) === */

extern void cmd_edit(const char *path);

extern void cmd_pt(const char *arg);

extern void cmd_show(const char *arg);

extern void cmd_go(const char *arg);

extern void cmd_where(const char *arg);

extern void cmd_clr(const char *arg);

extern void cmd_ver(const char *arg);

extern void cmd_help(const char *arg);

extern void cmd_schedpolicy(const char *arg);

extern void cmd_mempolicy(const char *arg);

extern void cmd_reboot(const char *arg);

extern void cmd_halt(const char *arg);

extern void cmd_shutdown(const char *arg);

extern void cmd_time_cmd(const char *arg);

extern void cmd_time(const char *arg);

extern void cmd_mem(const char *arg);

extern void cmd_dev(const char *arg);

extern void cmd_copy(const char *arg);

extern void cmd_del(const char *arg);

extern void cmd_mkdir(const char *arg);

extern void cmd_ren(const char *arg);

extern void cmd_type(const char *arg);

extern void cmd_find(const char *arg);

extern void cmd_size(const char *arg);

extern void cmd_echo(const char *arg);

extern void cmd_set(const char *arg);

extern void cmd_unset(const char *arg);

extern void cmd_run(const char *arg);

extern void cmd_load(const char *arg);

extern void cmd_append(const char *arg);

extern void cmd_setenv(const char *arg);

extern void cmd_unsetenv(const char *arg);

extern void cmd_bg(const char *arg);

extern void cmd_fg(const char *arg);

extern void cmd_jobs(const char *arg);

extern void cmd_nice(const char *arg);

extern void cmd_renice(const char *arg);

extern void cmd_nohup(const char *arg);

extern void cmd_watch(const char *arg);

extern void cmd_sleep(const char *arg);

extern void cmd_xargs(const char *arg);

extern void cmd_tee(const char *arg);

extern void cmd_install(const char *arg);

extern void cmd_which(const char *arg);

extern void cmd_logrotate(const char *arg);

extern void cmd_logrotate_ext(const char *arg);

extern void cmd_imgview(const char *arg);

extern void cmd_vol(const char *arg);

extern void cmd_sound(const char *arg);

extern void cmd_guistop(const char *arg);

extern void cmd_crepl(const char *arg);

extern void cmd_exec(const char *arg);

extern void cmd_gui(const char *arg);

extern void cmd_run_app(const char *arg);

extern void cmd_taskbar(const char *arg);

extern void cmd_search(const char *arg);

extern void cmd_fc(const char *arg);

extern void cmd_hash(const char *arg);

extern void cmd_save(const char *arg);

extern void cmd_resume(const char *arg);

extern void cmd_logout(const char *arg);

extern void cmd_test(const char *arg);

extern void cmd_expr(const char *arg);

extern void cmd_pidof(const char *arg);

extern void cmd_pstree(const char *arg);

extern void cmd_last(const char *arg);

extern void cmd_taskset(const char *arg);

extern void cmd_chrt(const char *arg);

extern void cmd_strace(const char *arg);

extern void cmd_lsof(const char *arg);

extern void cmd_prlimit(const char *arg);

extern void cmd_capsh(const char *arg);

extern void cmd_sysreport(const char *arg);

extern void cmd_dumpstack(const char *arg);

extern void cmd_kwork(const char *arg);

extern void cmd_ktrace(const char *arg);

extern void cmd_kprobe(const char *arg);

extern void cmd_notifier(const char *arg);

extern void cmd_sysctl(const char *arg);

extern void cmd_losetup(const char *arg);

extern void cmd_fallocate(const char *arg);

extern void cmd_filefrag(const char *arg);

extern void cmd_fsck(const char *arg);

extern void cmd_fsck_ext(const char *arg);

extern void cmd_sensors(const char *arg);

extern void cmd_cpufreq(const char *arg);

extern void cmd_i2c(const char *arg);

extern void cmd_spi(const char *arg);

extern void cmd_gpio(const char *arg);

extern void cmd_rtc(const char *arg);

extern void cmd_pinctrl(const char *arg);

extern void cmd_clk(const char *arg);

extern void cmd_dmaengine(const char *arg);

extern void cmd_mfd(const char *arg);

extern void cmd_devtmpfs(const char *arg);

extern void cmd_sysfs(const char *arg);

extern void cmd_netns(const char *arg);

extern void cmd_netfilter(const char *arg);

extern void cmd_seccomp(const char *arg);

extern void cmd_apparmor(const char *arg);

extern void cmd_keyring(const char *arg);

extern void cmd_audit(const char *arg);

extern void cmd_watchdog(const char *arg);

extern void cmd_applist(const char *arg);

extern void cmd_cpuidle(const char *arg);

extern void cmd_crypto(const char *arg);

extern void cmd_firmware(const char *arg);

extern void cmd_remoteproc(const char *arg);

extern void cmd_rpmsg(const char *arg);

extern void cmd_virtio(const char *arg);

extern void cmd_vmalloc(const char *arg);

extern void cmd_percpu(const char *arg);

extern void cmd_kfence(const char *arg);

extern void cmd_debugobj(const char *arg);

extern void cmd_lockdep(const char *arg);

extern void cmd_irqdomain(const char *arg);

extern void cmd_regmap(const char *arg);

extern void cmd_hwmon(const char *arg);

extern void cmd_ftrace(const char *arg);

extern void cmd_dmabuf(const char *arg);

extern void cmd_namespace(const char *arg);

extern void cmd_tracepoint(const char *arg);

extern void cmd_uprobe(const char *arg);

extern void cmd_kmod(const char *arg);

extern void cmd_iio(const char *arg);

extern void cmd_pwm(const char *arg);

extern void cmd_led(const char *arg);


extern void cmd_alias(const char *arg);

extern void cmd_apps(const char *arg);

extern void cmd_base64(const char *arg);

extern void cmd_calc(const char *arg);

extern void cmd_cat(const char *arg);

extern void cmd_cd(const char *arg);

extern void cmd_cedit(const char *arg);

extern void cmd_cgroup(const char *arg);

extern void cmd_chkdsk(const char *arg);

extern void cmd_chmod(const char *arg);

extern void cmd_chown(const char *arg);

extern void cmd_col(const char *arg);

extern void cmd_column(const char *arg);

extern void cmd_compress(const char *arg);

extern void cmd_config(const char *arg);

extern void cmd_db(const char *arg);

extern void cmd_dcache(const char *arg);

extern void cmd_dd_full(const char *arg);

extern void cmd_decompress(const char *arg);

extern void cmd_diff(const char *arg);

extern void cmd_du(const char *arg);

extern void cmd_env(const char *arg);

extern void cmd_epollinfo(const char *arg);

extern void cmd_evlog(const char *arg);

extern void cmd_fdisk(const char *arg);

extern void cmd_file(const char *arg);

extern void cmd_fim(const char *arg);

extern void cmd_flock(const char *arg);

extern void cmd_flock_cmd(const char *arg);

extern void cmd_fmt(const char *arg);

extern void cmd_format(const char *arg);

extern void cmd_freq(const char *arg);

extern void cmd_fsstat(const char *arg);

extern void cmd_futexinfo(const char *arg);

extern void cmd_grep(const char *arg);

extern void cmd_head(const char *arg);

extern void cmd_health(const char *arg);

extern void cmd_history(const char *arg);

extern void cmd_hrtimer(const char *arg);

extern void cmd_icache(const char *arg);

extern void cmd_inotifyinfo(const char *arg);

extern void cmd_iosched(const char *arg);

extern void cmd_ipc(const char *arg);

extern void cmd_kvm(const char *arg);

extern void cmd_ln(const char *arg);

extern void cmd_login(const char *arg);

extern void cmd_ls(const char *arg);

extern void cmd_lsblk(const char *arg);

extern void cmd_lscolor(const char *arg);

extern void cmd_lspci(const char *arg);

extern void cmd_lsusb(const char *arg);

extern void cmd_md5(const char *arg);

extern void cmd_mkfifo(const char *arg);

extern void cmd_mknod(const char *arg);

extern void cmd_mount(const char *arg);

extern void cmd_mount2(const char *arg);

extern void cmd_ntp(const char *arg);

extern void cmd_oom(const char *arg);

extern void cmd_pagecache(const char *arg);

extern void cmd_passwd(const char *arg);

extern void cmd_pkg(const char *arg);

extern void cmd_play(const char *arg);

extern void cmd_quota_ext(const char *arg);

extern void cmd_rcu(const char *arg);

extern void cmd_readahead_stat(const char *arg);

extern void cmd_readlink(const char *arg);

extern void cmd_reg(const char *arg);

extern void cmd_replace(const char *arg);

extern void cmd_slab(const char *arg);

extern void cmd_snapshot(const char *arg);

extern void cmd_softirq(const char *arg);

extern void cmd_sort(const char *arg);

extern void cmd_stat(const char *arg);

extern void cmd_su(const char *arg);

extern void cmd_sudo(const char *arg);

extern void cmd_syncstat(const char *arg);

extern void cmd_sysacct(const char *arg);

extern void cmd_sysrq(const char *arg);

extern void cmd_tail(const char *arg);

extern void cmd_taskmgr(const char *arg);

extern void cmd_telnet(const char *arg);

extern void cmd_tftp(const char *arg);

extern void cmd_touch(const char *arg);

extern void cmd_tree(const char *arg);

extern void cmd_ulimit(const char *arg);

extern void cmd_umount(const char *arg);

extern void cmd_uniq(const char *arg);

extern void cmd_useradd(const char *arg);

extern void cmd_userdel(const char *arg);

extern void cmd_version(const char *arg);

extern void cmd_watch_dir(const char *arg);

extern void cmd_wc(const char *arg);

extern void cmd_workqueue(const char *arg);

extern void cmd_xattr(const char *arg);


extern void cmd_df(void);

extern void cmd_fw(const char *arg);

extern void cmd_httpget(const char *a, const char *b);

extern void cmd_ipcs(void);

extern void cmd_netmon(void);

extern void cmd_pwd(void);

extern void cmd_sigstat(void);

extern void cmd_slabtop(void);

extern void cmd_ss(const char *arg);

extern void cmd_sync(void);


/* shell.c definitions */

extern void shell_print(const char *str);

extern int shell_last_exit_code;

extern int vbe_mode_active;


#endif
