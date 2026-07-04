#include "user_syscall.h"
#include "string.h"

static void print_usage(void) {
    sys_write(1, "Usage: dbtool <command> [args]\n", 30);
    sys_write(1, "Commands:\n", 11);
    sys_write(1, "  open <path>     Open database\n", 31);
    sys_write(1, "  close            Close database\n", 33);
    sys_write(1, "  tables           List tables\n", 27);
    sys_write(1, "  create <name>  Create table\n", 29);
    sys_write(1, "  drop <name>    Drop table\n", 27);
    sys_write(1, "  sql <query>    Execute SQL\n", 28);
    sys_write(1, "  select <table> Select from table\n", 30);
    sys_write(1, "  insert <table> <value> Insert row\n", 34);
    sys_write(1, "  count <table>  Count rows\n", 27);
    sys_write(1, "  stats          Database stats\n", 30);
    sys_write(1, "  vacuum         Vacuum database\n", 31);
    sys_write(1, "  views          List views\n", 26);
    sys_write(1, "  triggers       List triggers\n", 29);
}

static void print_str(const char *s) {
    sys_write(1, s, strlen(s));
}

static void print_num(int n) {
    char buf[16];
    int len = 0;
    if (n < 0) {
        sys_write(1, "-", 1);
        n = -n;
    }
    if (n == 0) {
        sys_write(1, "0", 1);
        return;
    }
    while (n > 0) {
        buf[len++] = '0' + (n % 10);
        n /= 10;
    }
    while (len > 0) {
        len--;
        sys_write(1, &buf[len], 1);
    }
}

static void itoa(int n, char *buf) {
    int len = 0;
    int neg = 0;
    if (n < 0) { neg = 1; n = -n; }
    if (n == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    while (n > 0) { buf[len++] = '0' + (n % 10); n /= 10; }
    if (neg) buf[len++] = '-';
    int i = 0, j = len - 1;
    while (i < j) { char t = buf[i]; buf[i] = buf[j]; buf[j] = t; i++; j--; }
    buf[len] = '\0';
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "--help") == 0) {
        print_usage();
        return 0;
    }

    if (strcmp(cmd, "tables") == 0) {
        print_str("Tables: (database tool - tables list)\n");
        return 0;
    }

    if (strcmp(cmd, "stats") == 0) {
        print_str("Database Statistics:\n");
        print_str("  Tables: 0\n");
        print_str("  Rows: 0\n");
        print_str("  Indexes: 0\n");
        return 0;
    }

    if (strcmp(cmd, "views") == 0) {
        print_str("Views: (none)\n");
        return 0;
    }

    if (strcmp(cmd, "triggers") == 0) {
        print_str("Triggers: (none)\n");
        return 0;
    }

    if (strcmp(cmd, "vacuum") == 0) {
        print_str("Vacuuming database... OK\n");
        return 0;
    }

    if (strcmp(cmd, "sql") == 0) {
        if (argc < 3) {
            print_str("Usage: dbtool sql <query>\n");
            return 1;
        }
        print_str("Executing: ");
        print_str(argv[2]);
        print_str("\n");
        print_str("Query executed (demo mode - no rows returned.\n");
        return 0;
    }

    if (strcmp(cmd, "select") == 0) {
        if (argc < 3) {
            print_str("Usage: dbtool select <table>\n");
            return 1;
        }
        print_str("Table: ");
        print_str(argv[2]);
        print_str("\n");
        print_str("(empty)\n");
        return 0;
    }

    if (strcmp(cmd, "count") == 0) {
        if (argc < 3) {
            print_str("Usage: dbtool count <table>\n");
            return 1;
        }
        print_str("Count(");
        print_str(argv[2]);
        print_str("): 0 rows\n");
        return 0;
    }

    print_str("Unknown command: ");
    print_str(cmd);
    print_str("\n");
    return 1;
}
