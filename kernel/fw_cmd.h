#ifndef FW_CMD_H
#define FW_CMD_H

/* Entry point for the firewall shell command.
 *
 *   fw                              -- show command summary
 *   fw status                       -- show counters / conntrack / NAT
 *   fw enable | disable             -- master switch
 *   fw tables                       -- list rule tables
 *   fw table create <name>          -- create a new table
 *   fw table attach <n> <chain>     -- attach a table to a hook
 *   fw chain list <table> [chain]   -- list rules
 *   fw policy <table> <chain> <a|d> -- set default policy
 *   fw conntrack [list|flush]       -- connection tracking
 *   fw nat list|add|delete|flush    -- NAT rules
 *   fw limit list|add|del|flush     -- bandwidth limits
 */
void cmd_fw(const char *line);

#endif
