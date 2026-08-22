/*
 * kernel/cmd_util2.h - Misc user-facing shell commands (PR-7, v0.9)
 *
 *   clr / clear           clear screen via ANSI
 *   ver                   print kernel version
 *   help [CMD]            list builtins or describe a command
 *   echo [-n] ARG         print arguments
 *   set [NAME=VAL]        set a shell variable
 *   unset NAME            drop a shell variable
 *   setenv NAME VAL       set an environment variable
 *   unsetenv NAME         drop an environment variable
 *   env                   list environment variables
 *   history               show command history (placeholder)
 *   alias NAME=VAL        define an alias
 *   unalias NAME          drop an alias
 *   find PATH             recursive search (placeholder)
 *   size PATH             print file size
 *   fc                    fix-command (history edit; placeholder)
 *   save / resume         checkpointing hooks
 *   logout                exit the shell
 *   edit PATH             invoke line-editor on a file (placeholder)
 *   pt / show / go / where navigation helpers
 *   copy / del / mkdir / ren  delegate to existing vfs helpers
 *   run / load / append    load-and-run helper
 *   taskbar / guistop / search / run_app / exec / gui / imgview / vol / sound / crepl
 *                         GUI app entrypoints (placeholders)
 *   logrotate / logrotate_ext  log rotation hooks
 *   fsck / fsck_ext / losetup / fallocate / filefrag  filesystem tools (placeholders)
 */
#ifndef _KERNEL_CMD_UTIL2_H
#define _KERNEL_CMD_UTIL2_H

void cmd_clr(const char *args);
void cmd_ver(const char *args);
void cmd_help(const char *args);
void cmd_echo(const char *args);
void cmd_set(const char *args);
void cmd_unset(const char *args);
void cmd_setenv(const char *args);
void cmd_unsetenv(const char *args);
void cmd_env(const char *args);
void cmd_history(const char *args);
void cmd_alias(const char *args);
void cmd_unalias(const char *args);
void cmd_find(const char *args);
void cmd_size(const char *args);
void cmd_fc(const char *args);
void cmd_save(const char *args);
void cmd_resume(const char *args);
void cmd_logout(const char *args);
void cmd_edit(const char *args);
void cmd_pt(const char *args);
void cmd_show(const char *args);
void cmd_go(const char *args);
void cmd_where(const char *args);
void cmd_copy(const char *args);
void cmd_del(const char *args);
void cmd_mkdir(const char *args);
void cmd_ren(const char *args);
void cmd_run(const char *args);
void cmd_load(const char *args);
void cmd_append(const char *args);
void cmd_taskbar(const char *args);
void cmd_guistop(const char *args);
void cmd_search(const char *args);
void cmd_run_app(const char *args);
void cmd_exec(const char *args);
void cmd_gui(const char *args);
void cmd_imgview(const char *args);
void cmd_vol(const char *args);
void cmd_sound(const char *args);
void cmd_crepl(const char *args);
void cmd_logrotate(const char *args);
void cmd_logrotate_ext(const char *args);
void cmd_fsck(const char *args);
void cmd_fsck_ext(const char *args);
void cmd_losetup(const char *args);
void cmd_fallocate(const char *args);
void cmd_filefrag(const char *args);

#endif