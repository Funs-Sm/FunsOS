/*
 * kernel/cmd_netdiag.c
 * cmd_iptraf, cmd_wol, cmd_ftp, cmd_speedtest
 *
 * Network diagnostic commands.
 */

#include "cmd_netdiag.h"
#include "shell.h"
#include "shell_error.h"
#include "stdio.h"
#include "string.h"

void cmd_iptraf(const char *args) {
    if (!args || !*args) {
        shell_print("iptraf - interactive IP traffic monitor\n");
        shell_print("Usage: iptraf [iface]\n");
        shell_print("  With no argument, shows traffic for all interfaces.\n");
        shell_print("  If iface is given, monitor only that interface.\n");
        shell_print("  Examples:\n");
        shell_print("    iptraf          # all interfaces\n");
        shell_print("    iptraf eth0     # only eth0\n");
        shell_last_exit_code = 0;
        return;
    }
    shell_print("iptraf: monitoring ");
    shell_print(args);
    shell_print(" (stub - live counters not yet wired)\n");
    shell_last_exit_code = 0;
}

void cmd_wol(const char *args) {
    if (!args || !*args) {
        shell_print("wol - send Wake-on-LAN magic packet\n");
        shell_print("Usage: wol <mac_address>\n");
        shell_print("  mac_address  Hex MAC with colons or dashes, e.g. 00:11:22:33:44:55\n");
        shell_print("  The packet is broadcast to UDP port 9 on the local network.\n");
        shell_last_exit_code = 1;
        return;
    }
    /* Validate MAC length loosely */
    int colons = 0;
    int len = 0;
    for (const char *p = args; *p; p++) {
        if (*p == ':' || *p == '-') colons++;
        else if ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F')) {
            len++;
        } else {
            shell_print("wol: invalid MAC character: ");
            shell_print(args);
            shell_print("\n");
            shell_last_exit_code = 1;
            return;
        }
    }
    if (colons != 5 || len != 12) {
        shell_print("wol: MAC must be 6 hex octets separated by ':' or '-'\n");
        shell_last_exit_code = 1;
        return;
    }
    shell_print("wol: sending magic packet to ");
    shell_print(args);
    shell_print(" (stub - network stack integration pending)\n");
    shell_last_exit_code = 0;
}

void cmd_ftp(const char *args) {
    if (!args || !*args) {
        shell_print("ftp - simple file transfer client\n");
        shell_print("Usage: ftp <command> [args]\n");
        shell_print("Subcommands:\n");
        shell_print("  open <host>      Connect to FTP server\n");
        shell_print("  close            Close current session\n");
        shell_print("  get <remote>     Download a file\n");
        shell_print("  put <local>      Upload a file\n");
        shell_print("  ls [path]        List remote directory\n");
        shell_print("  pwd              Print remote working directory\n");
        shell_print("  cd <path>        Change remote directory\n");
        shell_print("  quit             Exit ftp\n");
        shell_last_exit_code = 0;
        return;
    }
    shell_print("ftp: command '");
    shell_print(args);
    shell_print("' - stub, full client not yet implemented\n");
    shell_last_exit_code = 0;
}

void cmd_speedtest(const char *args) {
    if (!args || !*args) {
        shell_print("speedtest - measure network throughput\n");
        shell_print("Usage: speedtest [server]\n");
        shell_print("  With no argument, runs against the default test server.\n");
        shell_print("  The test loops 1 MB through the TCP stack and reports MB/s.\n");
        shell_last_exit_code = 0;
        return;
    }
    shell_print("speedtest: target ");
    shell_print(args);
    shell_print(" (stub - benchmark code not yet wired)\n");
    shell_last_exit_code = 0;
}
