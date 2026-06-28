/*
 * shell_error.c - FUNSOS Shell 独立错误处理模块实现
 *
 * 为每个 shell 指令提供差异化的、上下文相关的错误提示。
 * 根据不同的错误类型（缺少参数、参数无效、文件未找到等）
 * 给出针对具体指令的详细建议和修复指引。
 */

#include "shell_error.h"
#include "shell.h"
#include "string.h"

/* ================================================================
 *  通用错误处理 - 基础错误码映射
 * ================================================================ */

void shell_error(int err_code, const char *context) {
    switch (err_code) {
    case SHELL_ERR_UNKNOWN_CMD:
        shell_print("funs: unknown command '");
        if (context) shell_print(context);
        shell_print("' - type 'help' for available commands\n");
        break;
    case SHELL_ERR_FILE_NOT_FOUND:
        shell_print("funs: '");
        if (context) shell_print(context);
        shell_print("': No such file or directory\n");
        break;
    case SHELL_ERR_DIR_NOT_FOUND:
        shell_print("funs: '");
        if (context) shell_print(context);
        shell_print("': Directory not found\n");
        break;
    case SHELL_ERR_PERM_DENIED:
        shell_print("funs: '");
        if (context) shell_print(context);
        shell_print("': Permission denied\n");
        break;
    case SHELL_ERR_INVALID_ARG:
        shell_print("funs: invalid argument for '");
        if (context) shell_print(context);
        shell_print("'\n");
        break;
    case SHELL_ERR_MISSING_ARG:
        shell_print("funs: '");
        if (context) shell_print(context);
        shell_print("' requires an argument\n");
        break;
    case SHELL_ERR_TOO_MANY_ARGS:
        shell_print("funs: '");
        if (context) shell_print(context);
        shell_print("': too many arguments\n");
        break;
    case SHELL_ERR_NO_MEMORY:
        shell_print("funs: out of memory - system resources exhausted\n");
        break;
    case SHELL_ERR_READ_FAIL:
        shell_print("funs: failed to read '");
        if (context) shell_print(context);
        shell_print("'\n");
        break;
    case SHELL_ERR_WRITE_FAIL:
        shell_print("funs: failed to write '");
        if (context) shell_print(context);
        shell_print("'\n");
        break;
    case SHELL_ERR_CREATE_FAIL:
        shell_print("funs: cannot create '");
        if (context) shell_print(context);
        shell_print("': Operation failed\n");
        break;
    case SHELL_ERR_DELETE_FAIL:
        shell_print("funs: failed to delete '");
        if (context) shell_print(context);
        shell_print("'\n");
        break;
    case SHELL_ERR_RENAME_FAIL:
        shell_print("funs: failed to rename '");
        if (context) shell_print(context);
        shell_print("'\n");
        break;
    case SHELL_ERR_COPY_FAIL:
        shell_print("funs: failed to copy '");
        if (context) shell_print(context);
        shell_print("'\n");
        break;
    case SHELL_ERR_MOUNT_FAIL:
        shell_print("funs: failed to mount '");
        if (context) shell_print(context);
        shell_print("': Check device and filesystem type\n");
        break;
    case SHELL_ERR_UMOUNT_FAIL:
        shell_print("funs: failed to unmount '");
        if (context) shell_print(context);
        shell_print("': Device busy or not mounted\n");
        break;
    case SHELL_ERR_FORMAT_FAIL:
        shell_print("funs: format operation failed on '");
        if (context) shell_print(context);
        shell_print("'\n");
        break;
    case SHELL_ERR_DISK_FAIL:
        shell_print("funs: disk error on '");
        if (context) shell_print(context);
        shell_print("': I/O error\n");
        break;
    case SHELL_ERR_NOT_DIR:
        shell_print("funs: '");
        if (context) shell_print(context);
        shell_print("': Not a directory\n");
        break;
    case SHELL_ERR_NOT_FILE:
        shell_print("funs: '");
        if (context) shell_print(context);
        shell_print("': Not a regular file\n");
        break;
    case SHELL_ERR_IS_DIR:
        shell_print("funs: '");
        if (context) shell_print(context);
        shell_print("': Is a directory (use pt/ls for directories)\n");
        break;
    case SHELL_ERR_FILE_EXISTS:
        shell_print("funs: '");
        if (context) shell_print(context);
        shell_print("': File already exists\n");
        break;
    case SHELL_ERR_DIR_NOT_EMPTY:
        shell_print("funs: '");
        if (context) shell_print(context);
        shell_print("': Directory not empty (use del/rm -r to remove)\n");
        break;
    case SHELL_ERR_NO_SPACE:
        shell_print("funs: no space left on device\n");
        break;
    case SHELL_ERR_READ_ONLY:
        shell_print("funs: read-only filesystem - cannot write\n");
        break;
    case SHELL_ERR_NO_DEVICE:
        shell_print("funs: '");
        if (context) shell_print(context);
        shell_print("': No such device\n");
        break;
    case SHELL_ERR_DEVICE_BUSY:
        shell_print("funs: device is busy\n");
        break;
    case SHELL_ERR_NO_NETWORK:
        shell_print("funs: network interface is not available\n");
        shell_print("  Hint: Check network with 'ifconfig' first\n");
        break;
    case SHELL_ERR_CONN_FAIL:
        shell_print("funs: connection failed\n");
        break;
    case SHELL_ERR_TIMEOUT:
        shell_print("funs: operation timed out\n");
        break;
    case SHELL_ERR_DNS_FAIL:
        shell_print("funs: DNS resolution failed\n");
        shell_print("  Hint: Check DNS settings with 'dns'\n");
        break;
    case SHELL_ERR_NO_PROCESS:
        shell_print("funs: process '");
        if (context) shell_print(context);
        shell_print("' not found\n");
        shell_print("  Hint: Use 'ps' to list running processes\n");
        break;
    case SHELL_ERR_PROCESS_DEAD:
        shell_print("funs: process '");
        if (context) shell_print(context);
        shell_print("' has already terminated\n");
        break;
    case SHELL_ERR_SIGNAL_FAIL:
        shell_print("funs: failed to send signal to process\n");
        break;
    case SHELL_ERR_SYSCALL_FAIL:
        shell_print("funs: system call failed\n");
        break;
    case SHELL_ERR_NOT_IMPL:
        shell_print("funs: this feature is not yet implemented\n");
        break;
    case SHELL_ERR_INTERNAL:
        shell_print("funs: internal error in '");
        if (context) shell_print(context);
        shell_print("'\n");
        break;
    case SHELL_ERR_BAD_SYNTAX:
        shell_print("funs: syntax error near unexpected token '");
        if (context) shell_print(context);
        shell_print("'\n");
        break;
    case SHELL_ERR_NO_ALIAS:
        shell_print("funs: alias '");
        if (context) shell_print(context);
        shell_print("' not defined\n");
        break;
    case SHELL_ERR_ALIAS_EXISTS:
        shell_print("funs: alias '");
        if (context) shell_print(context);
        shell_print("' already exists (use 'alias <name>' to redefine)\n");
        break;
    case SHELL_ERR_LOOP:
        shell_print("funs: alias expansion loop detected for '");
        if (context) shell_print(context);
        shell_print("'\n");
        break;
    case SHELL_ERR_NO_HELP:
        shell_print("funs: no help available for '");
        if (context) shell_print(context);
        shell_print("'\n");
        shell_print("  Type 'help' without arguments to see all commands.\n");
        break;
    case SHELL_ERR_ABORTED:
        shell_print("funs: operation aborted by user (Ctrl+C)\n");
        break;
    default:
        shell_print("funs: unknown error occurred\n");
        break;
    }
}

/* ================================================================
 *  文件操作指令 - 差异化错误提示
 * ================================================================ */

void shell_err_pt(void) {
    shell_print("pt: Cannot list directory contents.\n");
    shell_print("  Reason: Current directory may not exist or is inaccessible.\n");
    shell_print("  Fix: Use 'go /' to return to root, then try 'pt' again.\n");
    shell_print("  Also try: ls, pwd\n");
}

void shell_err_show(const char *file) {
    shell_print("show: Cannot display file '");
    if (file) shell_print(file);
    shell_print("'.\n");
    shell_print("  Possible reasons:\n");
    shell_print("    - File does not exist (check spelling)\n");
    shell_print("    - Path is incorrect (use full path like '/dir/file')\n");
    shell_print("    - Permission denied (need read access)\n");
    shell_print("  Try: 'pt' or 'ls' to list files in current directory.\n");
    shell_print("  Also try: cat <file>, type <file>\n");
}

void shell_err_go(const char *dir) {
    shell_print("go: Cannot change to directory '");
    if (dir) shell_print(dir);
    shell_print("'.\n");
    shell_print("  Possible reasons:\n");
    shell_print("    - Directory does not exist\n");
    shell_print("    - Path is misspelled (check case sensitivity)\n");
    shell_print("    - Not a valid directory path\n");
    shell_print("  Tips:\n");
    shell_print("    - Use 'pt' or 'ls' to see available directories\n");
    shell_print("    - Use 'where' or 'pwd' to see current location\n");
    shell_print("    - Absolute paths start with '/' (e.g., '/usr/bin')\n");
    shell_print("  Also try: cd <dir>\n");
}

void shell_err_copy(const char *src) {
    shell_print("copy: Failed to copy '");
    if (src) shell_print(src);
    shell_print("'.\n");
    shell_print("  Usage: copy <source> <destination>\n");
    shell_print("  Examples:\n");
    shell_print("    copy readme.txt backup/readme.txt\n");
    shell_print("    copy /etc/config.txt ./config.bak\n");
    shell_print("  Check that source file exists and destination is writable.\n");
    shell_print("  Also try: cp <src> <dst>\n");
}

void shell_err_del(const char *file) {
    shell_print("del: Failed to delete '");
    if (file) shell_print(file);
    shell_print("'.\n");
    shell_print("  Possible reasons:\n");
    shell_print("    - File does not exist\n");
    shell_print("    - File is write-protected (permission denied)\n");
    shell_print("    - It's a directory (use a different command for dirs)\n");
    shell_print("  Usage: del <filename>\n");
    shell_print("  Examples: del temp.log, del old_file.txt\n");
    shell_print("  Also try: rm <file>\n");
}

void shell_err_mkdir(const char *dir) {
    shell_print("mkdir: Failed to create directory '");
    if (dir) shell_print(dir);
    shell_print("'.\n");
    shell_print("  Possible reasons:\n");
    shell_print("    - Parent directory does not exist\n");
    shell_print("    - Directory already exists\n");
    shell_print("    - Insufficient permissions or disk space\n");
    shell_print("    - Invalid directory name (contains special chars)\n");
    shell_print("  Usage: mkdir <directory_name>\n");
    shell_print("  Example: mkdir projects, mkdir /tmp/newdir\n");
}

void shell_err_ren(void) {
    shell_print("ren: Failed to rename file/directory.\n");
    shell_print("  Usage: ren <old_name> <new_name>\n");
    shell_print("  Examples:\n");
    shell_print("    ren oldname.txt newname.txt\n");
    shell_print("    ren myfile.c main.c\n");
    shell_print("  Note: Source must exist; target must not exist.\n");
    shell_print("  Also try: mv <old> <new>\n");
}

void shell_err_find(void) {
    shell_print("find: File search failed.\n");
    shell_print("  Usage: find <filename_pattern>\n");
    shell_print("  Examples:\n");
    shell_print("    find config     -- search for 'config' in current dir recursively\n");
    shell_print("    find .c         -- find all .c files\n");
    shell_print("    find readme     -- locate any file named 'readme'\n");
    shell_print("  Make sure you're in a valid directory (use 'pwd' to check).\n");
}

void shell_err_size(const char *file) {
    shell_print("size: Cannot get size of '");
    if (file) shell_print(file);
    shell_print("'.\n");
    shell_print("  The specified file was not found or is not accessible.\n");
    shell_print("  Usage: size <filepath>\n");
    shell_print("  Example: size /initrd/readme.txt\n");
}

void shell_err_echo(void) {
    shell_print("echo: Nothing to display.\n");
    shell_print("  Usage: echo <text>\n");
    shell_print("  Examples:\n");
    shell_print("    echo Hello World\n");
    shell_print("    echo $HOME      -- print environment variable\n");
    shell_print("    echo \"line one\" > file.txt  -- write to file\n");
}

void shell_err_set(void) {
    shell_print("set: Failed to set environment variable.\n");
    shell_print("  Usage: set <VARNAME> <value>\n");
    shell_print("  Examples:\n");
    shell_print("    set EDITOR vi\n");
    shell_print("    set PATH /bin:/usr/bin\n");
    shell_print("    set MY_VAR hello_world\n");
    shell_print("  Variable names should be uppercase by convention.\n");
    shell_print("  Use 'env' to list all current variables.\n");
}

void shell_err_run(const char *file) {
    shell_print("run: Cannot execute '");
    if (file) shell_print(file);
    shell_print("'.\n");
    shell_print("  Possible reasons:\n");
    shell_print("    - File does not exist (check path)\n");
    shell_print("    - Not an executable file (wrong format)\n");
    shell_print("    - Permission denied (not executable)\n");
    shell_print("  Usage: run <program_path> [arguments...]\n");
    shell_print("  Examples: run /bin/testapp, run init\n");
    shell_print("  Tip: Use 'which <cmd>' to find program locations.\n");
}

void shell_err_cat(const char *file) {
    shell_print("cat: Cannot display '");
    if (file) shell_print(file);
    shell_print("'.\n");
    shell_print("  Usage: cat <filename>\n");
    shell_print("  Examples: cat readme.txt, cat /etc/hostname\n");
    shell_print("  For long files, use: head <file> or tail <file>\n");
    shell_print("  Also try: show <file>, type <file>\n");
}

void shell_err_ls(void) {
    shell_print("ls: Cannot list directory.\n");
    shell_print("  The current directory may be invalid or inaccessible.\n");
    shell_print("  Fix: Try 'cd /' then 'ls', or use 'pt' instead.\n");
}

void shell_err_cd(const char *dir) {
    shell_print("cd: Cannot change to '");
    if (dir) shell_print(dir);
    shell_print("'.\n");
    shell_print("  This directory does not exist or you lack permission.\n");
    shell_print("  Suggestions:\n");
    shell_print("    - Use 'ls' or 'pt' to browse available directories\n");
    shell_print("    - Use 'pwd' to see where you are now\n");
    shell_print("    - Check for typos (case-sensitive!)\n");
    shell_print("  Also try: go <dir>\n");
}

void shell_err_touch(const char *file) {
    shell_print("touch: Cannot create '");
    if (file) shell_print(file);
    shell_print("'.\n");
    shell_print("  Possible reasons:\n");
    shell_print("    - Parent directory doesn't exist\n");
    shell_print("    - A directory with this name already exists\n");
    shell_print("    - Disk is full or filesystem is read-only\n");
    shell_print("  Usage: touch <filename>\n");
    shell_print("  Example: touch newfile.txt, touch /tmp/log\n");
}

void shell_err_append(const char *file) {
    shell_print("append: Cannot append to '");
    if (file) shell_print(file);
    shell_print("'.\n");
    shell_print("  Usage: append <filename> <text>\n");
    shell_print("  Examples:\n");
    shell_print("    append log.txt \"new entry\"\n");
    shell_print("    append notes.md \"- item added\"\n");
    shell_print("  The file will be created if it doesn't exist.\n");
}

void shell_err_head(const char *file) {
    shell_print("head: Cannot show head of '");
    if (file) shell_print(file);
    shell_print("'.\n");
    shell_print("  Usage: head <filename> [number_of_lines]\n");
    shell_print("  Examples:\n");
    shell_print("    head readme.txt       -- show first 10 lines (default)\n");
    shell_print("    head log.txt 20       -- show first 20 lines\n");
    shell_print("  Also try: tail <file> [N] for end of file\n");
}

void shell_err_tail(const char *file) {
    shell_print("tail: Cannot show tail of '");
    if (file) shell_print(file);
    shell_print("'.\n");
    shell_print("  Usage: tail <filename> [number_of_lines]\n");
    shell_print("  Examples:\n");
    shell_print("    tail logfile.txt      -- show last 10 lines (default)\n");
    shell_print("    tail syslog 50        -- show last 50 lines\n");
    shell_print("  Useful for monitoring logs in real-time.\n");
}

void shell_err_wc(const char *file) {
    shell_print("wc: Cannot count words in '");
    if (file) shell_print(file);
    shell_print("'.\n");
    shell_print("  Usage: wc <filename>\n");
    shell_print("  Displays: line_count  word_count  char_count  filename\n");
    shell_print("  Example: wc readme.txt -> \"42  256  1530  readme.txt\"\n");
}

void shell_err_diff(void) {
    shell_print("diff: Cannot compare files.\n");
    shell_print("  Usage: diff <file1> <file2>\n");
    shell_print("  Compares two files line by line and shows differences.\n");
    shell_print("  Examples:\n");
    shell_print("    diff file.old file.new\n");
    shell_print("    diff /etc/config.orig /etc/config\n");
    shell_print("  Both files must exist and be readable.\n");
}

void shell_err_sort(const char *file) {
    shell_print("sort: Cannot sort '");
    if (file) shell_print(file);
    shell_print("'.\n");
    shell_print("  Usage: sort <filename>\n");
    shell_print("  Sorts lines alphabetically. Output goes to screen.\n");
    shell_print("  To save sorted output: sort data.txt | tee sorted.txt\n");
}

void shell_err_uniq(const char *file) {
    shell_print("uniq: Cannot process '");
    if (file) shell_print(file);
    shell_print("'.\n");
    shell_print("  Usage: uniq <filename>\n");
    shell_print("  Removes adjacent duplicate lines from sorted input.\n");
    shell_print("  For best results, pipe through sort first:\n");
    shell_print("    sort data.txt | uniq\n");
}

void shell_err_grep(void) {
    shell_print("grep: Pattern search failed.\n");
    shell_print("  Usage: grep <pattern> <filename>\n");
    shell_print("  Searches for text pattern in a file (case-insensitive).\n");
    shell_print("  Examples:\n");
    shell_print("    grep error syslog.txt   -- find lines containing 'error'\n");
    shell_print("    grep \"TODO\" *.c        -- find TODO comments (use quotes)\n");
    shell_print("  Supports basic substring matching.\n");
}

void shell_err_replace(void) {
    shell_print("replace: Text replacement failed.\n");
    shell_print("  Usage: replace <old_text> <new_text> <filename>\n");
    shell_print("  Examples:\n");
    shell_print("    replace foo bar data.txt  -- replace 'foo' with 'bar'\n");
    shell_print("    replace \"old ver\" \"v2\" release.txt\n");
    shell_print("  Warning: Replaces ALL occurrences in the file.\n");
}

void shell_err_chmod(void) {
    shell_print("chmod: Failed to change permissions.\n");
    shell_print("  Usage: chmod <mode> <filename>\n");
    shell_print("  Mode examples:\n");
    shell_print("    chmod 755 script.sh   -- rwxr-xr-x (executable)\n");
    shell_print("    chmod 644 file.txt   -- rw-r--r-- (normal file)\n");
    shell_print("    chmod 600 secret.key  -- rw------- (private)\n");
}

void shell_err_chown(void) {
    shell_print("chown: Failed to change owner.\n");
    shell_print("  Usage: chown <username> <filename>\n");
    shell_print("  Example: chown admin config.txt\n");
    shell_print("  Note: Requires root/admin privileges.\n");
}

void shell_err_stat(const char *file) {
    shell_print("stat: Cannot get metadata for '");
    if (file) shell_print(file);
    shell_print("'.\n");
    shell_print("  Usage: stat <filename>\n");
    shell_print("  Shows file size, permissions, timestamps, inode info.\n");
    shell_print("  Example: stat /initrd/kernel.bin\n");
}

void shell_err_tree(const char *dir) {
    shell_print("tree: Cannot display tree for '");
    if (dir) shell_print(dir);
    shell_print("'.\n");
    shell_print("  Usage: tree [directory_path]\n");
    shell_print("  Shows recursive directory structure.\n");
    shell_print("  Examples:\n");
    shell_print("    tree           -- tree of current directory\n");
    shell_print("    tree /initrd    -- tree of /initrd\n");
}

void shell_err_du(const char *dir) {
    shell_print("du: Cannot check disk usage for '");
    if (dir) shell_print(dir);
    shell_print("'.\n");
    shell_print("  Usage: du [directory_path]\n");
    shell_print("  Shows total disk usage of directory (recursive).\n");
    shell_print("  Example: du /initrd, du .\n");
}

void shell_err_edit(const char *file) {
    shell_print("edit: Cannot open editor for '");
    if (file) shell_print(file);
    shell_print("'.\n");
    shell_print("  Usage: edit <filename>\n");
    shell_print("  Opens the built-in text editor.\n");
    shell_print("  If file exists, it loads for editing; else creates new file.\n");
    shell_print("  Controls: arrows=move, insert=text, Ctrl+S=save, Ctrl+Q=quit\n");
}

/* ================================================================
 *  系统指令 - 差异化错误提示
 * ================================================================ */

void shell_err_ping(const char *arg) {
    shell_print("ping: Invalid argument '");
    if (arg) shell_print(arg);
    shell_print("'.\n");
    shell_print("  Usage: ping <ip_address_or_hostname>\n");
    shell_print("  Examples:\n");
    shell_print("    ping 192.168.1.1\n");
    shell_print("    ping google.com\n");
    shell_print("    ping 10.0.0.1\n");
    shell_print("  Sends ICMP echo requests. Press Ctrl+C to stop.\n");
    shell_print("  Prerequisite: Network must be up ('ifconfig').\n");
}

void shell_err_kill(const char *pid_str) {
    shell_print("kill: Cannot terminate process '");
    if (pid_str) shell_print(pid_str);
    shell_print("'.\n");
    shell_print("  Possible reasons:\n");
    shell_print("    - Process ID is invalid (must be a number)\n");
    shell_print("    - Process does not exist or has already exited\n");
    shell_print("    - You don't have permission to kill this process\n");
    shell_print("  Usage: kill <PID>\n");
    shell_print("  Step 1: Use 'ps' to find the PID of the target process\n");
    shell_print("  Step 2: kill <PID>\n");
    shell_print("  Example: kill 42\n");
}

void shell_err_mount(const char *dev) {
    shell_print("mount: Failed to mount '");
    if (dev) shell_print(dev);
    shell_print("'.\n");
    shell_print("  Usage: mount <device> <mount_point>\n");
    shell_print("  Examples:\n");
    shell_print("    mount /dev/hda1 /mnt/disk\n");
    shell_print("    mount /dev/cdrom /mnt/cdrom\n");
    shell_print("  Common issues:\n");
    shell_print("    - Device doesn't exist (check with 'lsblk')\n");
    shell_print("    - Mount point directory doesn't exist (create it first)\n");
    shell_print("    - Filesystem type not supported\n");
}

void shell_err_umount(const char *dir) {
    shell_print("umount: Failed to unmount '");
    if (dir) shell_print(dir);
    shell_print("'.\n");
    shell_print("  Possible reasons:\n");
    shell_print("    - Directory is not a mount point\n");
    shell_print("    - Device is busy (files still open or cwd inside)\n");
    shell_print("    - Need root privileges\n");
    shell_print("  Usage: umount <mount_point>\n");
    shell_print("  Example: umount /mnt/disk\n");
}

void shell_err_format(void) {
    shell_print("format: Formatting operation failed.\n");
    shell_print("  Usage: format <device> <filesystem_type>\n");
    shell_print("  Examples:\n");
    shell_print("    format /dev/hda1 fat32\n");
    shell_print("    format /dev/sdb1 ext2\n");
    shell_print("  WARNING: This destroys ALL data on the device!\n");
    shell_print("  Supported types: fat32, ext2\n");
}

void shell_err_fdisk(void) {
    shell_print("fdisk: Partition operation failed.\n");
    shell_print("  Usage: fdisk <device>\n");
    shell_print("  Examples:\n");
    shell_print("    fdisk /dev/hda\n");
    shell_print("    fdisk /dev/sdb\n");
    shell_print("  Shows or edits the partition table of a block device.\n");
    shell_print("  Use 'lsblk' to list available devices first.\n");
}

void shell_err_chkdsk(void) {
    shell_print("chkdsk: Disk check failed.\n");
    shell_print("  Usage: chkdsk <device>\n");
    shell_print("  Examples:\n");
    shell_print("    chkdsk /dev/hda1\n");
    shell_print("    chkdsk /dev/sda1\n");
    shell_print("  Checks filesystem integrity and repairs errors.\n");
}

void shell_err_calc(void) {
    shell_print("calc: Expression evaluation failed.\n");
    shell_print("  Usage: calc <mathematical_expression>\n");
    shell_print("  Supported operators: + - * / ( )\n");
    shell_print("  Examples:\n");
    shell_print("    calc 2+2              => 4\n");
    shell_print("    calc (3+5)*4          => 32\n");
    shell_print("    calc 100/7            => 14 (integer division)\n");
    shell_print("    calc 1024*1024        => 1048576\n");
    shell_print("  Note: Only integer arithmetic. Left-to-right eval for same precedence.\n");
}

void shell_err_fg(const char *pid_str) {
    shell_print("fg: Cannot bring process to foreground.\n");
    if (pid_str && *pid_str) {
        shell_print("  Process '");
        shell_print(pid_str);
        shell_print("' is not a valid background job.\n");
    }
    shell_print("  Usage: fg <job_id_or_PID>\n");
    shell_print("  First, start a job in background:\n");
    shell_print("    sleep 100 &          -- runs in background\n");
    shell_print("    jobs                 -- list background jobs\n");
    shell_print("    fg %1                -- bring job #1 to foreground\n");
}

void shell_err_bg(const char *pid_str) {
    shell_print("bg: Cannot send process to background.\n");
    shell_print("  Usage: bg <PID>  (or use '&' after command)\n");
    shell_print("  Easier method: add '&' at end of command:\n");
    shell_print("    long_task &           -- runs in background immediately\n");
    shell_print("    jobs                  -- see background tasks\n");
}

void shell_err_nice(void) {
    shell_print("nice: Failed to set process priority.\n");
    shell_print("  Usage: nice <PID> <priority>\n");
    shell_print("  Priority range: -20 (highest) to 19 (lowest), default 0\n");
    shell_print("  Examples:\n");
    shell_print("    nice 42 10           -- lower priority of PID 42\n");
    shell_print("    nice 15 -5           -- raise priority of PID 15\n");
    shell_print("  Use 'ps' to find PIDs. Root can set negative priorities.\n");
}

void shell_err_renice(void) {
    shell_print("renice: Failed to change priority.\n");
    shell_print("  Usage: renice <PID> <new_priority>\n");
    shell_print("  Same as 'nice' but for running processes.\n");
    shell_print("  Example: renice 1234 5\n");
}

void shell_err_watch(void) {
    shell_print("watch: Command execution failed.\n");
    shell_print("  Usage: watch <command>\n");
    shell_print("  Repeats a command every 2 seconds (like top but custom).\n");
    shell_print("  Examples:\n");
    shell_print("    watch ps             -- refresh process list every 2s\n");
    shell_print("    watch df             -- monitor disk space\n");
    shell_print("    watch free           -- monitor memory usage\n");
    shell_print("  Press Ctrl+C to stop watching.\n");
}

void shell_err_sleep(void) {
    shell_print("sleep: Invalid argument.\n");
    shell_print("  Usage: sleep <seconds>\n");
    shell_print("  Pauses execution for N seconds.\n");
    shell_print("  Examples:\n");
    shell_print("    sleep 5     -- wait 5 seconds\n");
    shell_print("    sleep 60    -- wait 1 minute\n");
    shell_print("    sleep 300   -- wait 5 minutes\n");
    shell_print("  Combine with &: 'sleep 100 &' to background.\n");
}

void shell_err_test(void) {
    shell_print("test: Conditional expression evaluation failed.\n");
    shell_print("  Usage: test <expression>  or  [ <expression> ]\n");
    shell_print("  Supported operators:\n");
    shell_print("    -z STR    true if string is empty\n");
    shell_print("    -n STR    true if string is not empty\n");
    shell_print("    STR1=STR2 true if strings are equal\n");
    shell_print("    STR1!=STR2 true if strings differ\n");
    shell_print("    -e FILE   true if file exists\n");
    shell_print("    -d DIR    true if directory exists\n");
    shell_print("    eq/ne/gt/lt for numeric comparisons\n");
    shell_print("  Examples:\n");
    shell_print("    test -z \"$VAR\" && echo empty\n");
    shell_print("    test -e /initrd/kernel.bin && echo found\n");
}

void shell_err_expr(void) {
    shell_print("expr: Mathematical expression error.\n");
    shell_print("  Usage: expr <expression>\n");
    shell_print("  Evaluates math expressions and prints result.\n");
    shell_print("  Operators: + - * / % ( )\n");
    shell_print("  Examples:\n");
    shell_print("    expr 2 + 3            => 5\n");
    shell_print("    expr 10 * 20          => 200\n");
    shell_print("    expr (100 + 50) / 3   => 50\n");
}

void shell_err_xargs(void) {
    shell_print("xargs: Missing command to execute.\n");
    shell_print("  Usage: xargs <command>\n");
    shell_print("  Reads items from stdin and executes command with them.\n");
    shell_print("  Example: echo file1 file2 file3 | xargs wc\n");
    shell_print("  Runs: wc file1; wc file2; wc file3\n");
}

void shell_err_tee(const char *file) {
    shell_print("tee: Cannot write to '");
    if (file) shell_print(file);
    shell_print("'.\n");
    shell_print("  Usage: tee <filename>\n");
    shell_print("  Reads stdin, writes to both stdout AND file.\n");
    shell_print("  Examples:\n");
    shell_print("    ls | tee listing.txt    -- saves ls output to file too\n");
    shell_print("    dmesg | tee boot.log     -- capture boot messages\n");
}

void shell_err_install(void) {
    shell_print("install: Installation failed.\n");
    shell_print("  Usage: install <source> <destination>\n");
    shell_print("  Like 'copy' but also sets executable permission.\n");
    shell_print("  Example: install myprog /bin/myprog\n");
}

void shell_err_which(const char *cmd) {
    shell_print("which: Cannot locate '");
    if (cmd) shell_print(cmd);
    shell_print("'.\n");
    shell_print("  Usage: which <command_name>\n");
    shell_print("  Shows the full path of a command's executable.\n");
    shell_print("  Examples:\n");
    shell_print("    which ls      => /bin/ls\n");
    shell_print("    which cat     => /bin/cat\n");
    shell_print("  If not found, the command is either builtin or absent.\n");
}

void shell_err_pkg(const char *subcmd) {
    shell_print("pkg: Package management error.\n");
    shell_print("  Available subcommands:\n");
    shell_print("    pkg install <name>   Install a package\n");
    shell_print("    pkg remove <name>    Remove (uninstall) a package\n");
    shell_print("    pkg update <name>    Update an installed package\n");
    shell_print("    pkg list             List installed packages\n");
    shell_print("    pkg search <name>    Search available packages\n");
    shell_print("  Examples:\n");
    shell_print("    pkg install vim\n");
    shell_print("    pkg remove vim\n");
    shell_print("    pkg list\n");
    shell_print("    pkg search editor\n");
    if (subcmd && *subcmd) {
        shell_print("  Your input: pkg ");
        shell_print(subcmd);
        shell_print(" -- missing required arguments for this subcommand.\n");
    }
}

void shell_err_httpget(void) {
    shell_print("httpget: HTTP request failed.\n");
    shell_print("  Usage: httpget <url>\n");
    shell_print("  Performs an HTTP GET request and prints the response.\n");
    shell_print("  Examples:\n");
    shell_print("    httpget http://example.com\n");
    shell_print("    httpget http://localhost:8080/api/status\n");
    shell_print("  Requires active network connection.\n");
}

void shell_err_imgview(void) {
    shell_print("imgview: Image viewing failed.\n");
    shell_print("  Usage: imgview <image_path>\n");
    shell_print("  Supported formats: JPEG (.jpg, .jpeg), PNG (.png)\n");
    shell_print("  Examples:\n");
    shell_print("    imgview photo.jpg\n");
    shell_print("    imgview /pics/screenshot.png\n");
    shell_print("  Requires GUI display server (start with 'gui' first).\n");
}

void shell_err_freq(void) {
    shell_print("freq: CPU frequency operation failed.\n");
    shell_print("  Usage: freq [frequency_in_MHz]\n");
    shell_print("  With no args: shows current CPU frequency estimate.\n");
    shell_print("  With arg: sets CPU frequency (stub).\n");
    shell_print("  Examples:\n");
    shell_print("    freq           => CPU frequency: ~1000 MHz\n");
    shell_print("    freq 2000      => Set to 2000 MHz (stub)\n");
}

void shell_err_fw(void) {
    shell_print("fw: Firmware command failed.\n");
    shell_print("  Usage: fw <subcommand> [args...]\n");
    shell_print("  Manages system firmware and low-level hardware.\n");
    shell_print("  Use 'fw help' for available subcommands.\n");
}

void shell_err_kvm(void) {
    shell_print("kvm: Virtual machine operation failed.\n");
    shell_print("  Usage: kvm\n");
    shell_print("  Starts the KVM (Kernel-based Virtual Machine) manager.\n");
    shell_print("  Requires hardware virtualization support (Intel VT-x / AMD-V).\n");
    shell_print("  This is an advanced feature - ensure CPU supports VM extensions.\n");
}

void shell_err_crepl(void) {
    shell_print("c/crepl: C interpreter failed to start.\n");
    shell_print("  Usage: c  or  crepl\n");
    shell_print("  Starts an interactive C language REPL (Read-Eval-Print Loop).\n");
    shell_print("  You can type C expressions and statements directly:\n");
    shell_print("    > int x = 42;\n");
    shell_print("    > printf(\"x = %d\\n\", x);\n");
    shell_print("    > x * 2\n");
    shell_print("  Type 'quit' or press Ctrl+C to exit.\n");
}

void shell_err_alias(const char *arg) {
    shell_print("alias: Alias operation failed.\n");
    shell_print("  Usage: alias <name>=<command>  OR  alias <name> (to show)\n");
    shell_print("  Examples:\n");
    shell_print("    alias ll='ls -l'      -- create shorthand\n");
    shell_print("    alias cls=clear        -- rename command\n");
    shell_print("    alias ll               -- show what 'll' expands to\n");
    if (arg && *arg) {
        shell_print("  Issue with alias '");
        shell_print(arg);
        shell_print("': name may contain invalid characters or already exist.\n");
    }
}

void shell_err_help(const char *arg) {
    shell_print("help: No help available for '");
    if (arg) shell_print(arg);
    shell_print("'.\n");
    shell_print("  Type 'help' alone to list all available commands.\n");
    shell_print("  Or try one of these common commands:\n");
    shell_print("    help ls, help cd, help cat, help ping, help ps\n");
    shell_print("    help pkg, help mount, help edit, help calc\n");
}

void shell_err_logrotate(void) {
    shell_print("logrotate: Log rotation operation failed.\n");
    shell_print("  Usage: logrotate [config_file]\n");
    shell_print("  Rotates, compresses, and removes old log files.\n");
    shell_print("  Without args: uses default configuration.\n");
    shell_print("  Run as root for best results.\n");
}

void shell_err_syslog(void) {
    shell_print("syslog: System log management error.\n");
    shell_print("  Subcommands:\n");
    shell_print("    syslog show              Show current rules\n");
    shell_print("    syslog add <fac> <lvl> <target>  Add rule\n");
    shell_print("    syslog del <index>       Remove rule at index\n");
    shell_print("    syslog level <0-7|name>   Set global log level\n");
    shell_print("  Facilities: kern, user, mail, daemon, auth, syslog, lpr, news\n");
    shell_print("  Levels: EMERG(0) ALERT(1) CRIT(2) ERR(3) WARN(4) NOTICE(5) INFO(6) DEBUG(7)\n");
    shell_print("  Targets: console, file=<path>\n");
    shell_print("  Example: syslog add kern err console\n");
}

/* ================================================================
 *  未知指令错误
 * ================================================================ */

void shell_err_unknown(const char *cmd) {
    shell_print("funs: '");
    if (cmd) shell_print(cmd);
    shell_print("': command not found\n");

    if (cmd && cmd[0]) {
        const char *suggestions[] = {
            "ls", "pt", "cat", "show", "type", "cd", "go", "pwd", "where",
            "echo", "help", "clear", "clr", "logout", "login", "whoami", "id", "umask",
            "mkdir", "rm", "del", "cp", "copy", "mv", "ren", "touch",
            "ps", "kill", "reboot", "halt", "shutdown", "dmesg", "uname",
            "date", "time", "ifconfig", "ping", "history", "export", "set",
            "chmod", "chown", "free", "df", "mount", "umount", "vi", "edit",
            NULL
        };
        int best_dist = 999;
        const char *best_match = NULL;
        int best2_dist = 999;
        const char *best2_match = NULL;

        int cmdlen = 0;
        while (cmd[cmdlen]) cmdlen++;

        for (int i = 0; suggestions[i]; i++) {
            const char *s = suggestions[i];
            int slen = 0;
            while (s[slen]) slen++;
            int dist = 0;
            int maxlen = cmdlen > slen ? cmdlen : slen;
            int minlen = cmdlen < slen ? cmdlen : slen;
            dist = maxlen - minlen;
            for (int j = 0; j < minlen; j++) {
                if (cmd[j] != s[j]) dist++;
            }
            if (dist < best_dist) {
                best2_dist = best_dist;
                best2_match = best_match;
                best_dist = dist;
                best_match = s;
            } else if (dist < best2_dist) {
                best2_dist = dist;
                best2_match = s;
            }
        }

        if (best_match && best_dist <= 3) {
            shell_print("\n  Did you mean: ");
            shell_print(best_match);
            if (best2_match && best2_dist <= 4) {
                shell_print(", ");
                shell_print(best2_match);
            }
            shell_print("?\n");
        }
    }

    shell_print("  Type 'help' for available commands.\n");
}

void shell_err_wget(const char *url) {
    shell_print("wget: Failed to download '");
    if (url) shell_print(url);
    shell_print("'.\n");
    shell_print("  Usage: wget <url>\n");
    shell_print("  Downloads a file from a URL via HTTP.\n");
    shell_print("  Examples:\n");
    shell_print("    wget http://example.com/file.txt\n");
    shell_print("    wget http://10.0.0.1/config.ini\n");
    shell_print("  Prerequisites: Network up ('ifconfig'), DNS configured ('dns')\n");
}

void shell_err_traceroute(const char *ip) {
    shell_print("traceroute: Failed to trace route to '");
    if (ip) shell_print(ip);
    shell_print("'.\n");
    shell_print("  Usage: traceroute <ip_or_hostname>\n");
    shell_print("  Shows the network path (hops) to the destination.\n");
    shell_print("  Each line shows one hop with its IP and round-trip time.\n");
    shell_print("  Example: traceroute 8.8.8.8\n");
    shell_print("  Requires network connectivity and ICMP support.\n");
}

void shell_err_base64(const char *file) {
    shell_print("base64: Cannot process '");
    if (file) shell_print(file);
    shell_print("'.\n");
    shell_print("  Usage: base64 <filename>\n");
    shell_print("  Encodes or decodes a file using Base64 encoding.\n");
    shell_print("  Output is printed to stdout (pipe to file to save).\n");
    shell_print("  Example: base64 secret.bin > secret.b64\n");
}

void shell_err_md5(const char *file) {
    shell_print("md5: Cannot compute hash for '");
    if (file) shell_print(file);
    shell_print("'.\n");
    shell_print("  Usage: md5 <filename>\n");
    shell_print("  Computes and displays the MD5 checksum of a file.\n");
    shell_print("  Useful for verifying file integrity.\n");
    shell_print("  Example: md5 kernel.bin\n");
    shell_print("  Output format: MD5 (<filename>) = <hash_string>\n");
}

void shell_err_exec(const char *file) {
    shell_print("exec: Cannot execute '");
    if (file) shell_print(file);
    shell_print("'.\n");
    shell_print("  Usage: exec <program> [args...]\n");
    shell_print("  Replaces the current process with the specified program.\n");
    shell_print("  Unlike 'run', exec does NOT return on success.\n");
    shell_print("  Examples:\n");
    shell_print("    exec /bin/init\n");
    shell_print("    exec myapp arg1 arg2\n");
    shell_print("  Check that the program exists: ls /bin/, which <name>\n");
}

void shell_err_nohup(void) {
    shell_print("nohup: Background execution failed.\n");
    shell_print("  Usage: nohup <command>\n");
    shell_print("  Runs a command that continues running even after logout.\n");
    shell_print("  Output is redirected to nohup.out by default.\n");
    shell_print("  Examples:\n");
    shell_print("    nohup long_task &\n");
    shell_print("    nohup ./server &\n");
    shell_print("  Tip: Use 'jobs' to check status, 'fg' to bring back to foreground.\n");
}
