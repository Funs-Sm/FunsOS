/*
 * kernel/cmd_text.c - P4: Text utility commands
 * cut, paste, tr, rev, fold, expand, unexpand, nl, look, comm, tsort
 */

#include "cmd_text.h"
#include "shell.h"
#include "shell_error.h"
#include "string.h"
#include "stdio.h"

void cmd_cut(const char *args) {
    if (!args || !*args) {
        shell_print("cut - print selected parts of lines\n");
        shell_print("Usage: cut OPTION... [FILE]...\n");
        shell_print("Options:\n");
        shell_print("  -b LIST      Select only these bytes (e.g. 1,3-5,7-)\n");
        shell_print("  -c LIST      Select only these characters\n");
        shell_print("  -f LIST      Select only these fields (default delimiter: TAB)\n");
        shell_print("  -d DELIM     Field delimiter (default TAB)\n");
        shell_print("  -s           Do not print lines that contain no delimiters\n");
        shell_print("  --complement Invert the selection\n");
        shell_print("\nIf no FILE is given, or FILE is '-', read from stdin.\n");
        shell_last_exit_code = 1;
        return;
    }
    shell_print("cut: stub (option parser not yet implemented)\n");
    shell_print("  requested: ");
    shell_print(args);
    shell_print("\n");
    shell_last_exit_code = 0;
}

void cmd_paste(const char *args) {
    if (!args || !*args) {
        shell_print("paste - merge lines from multiple files\n");
        shell_print("Usage: paste [OPTION]... [FILE]...\n");
        shell_print("  -d LIST      Use characters from LIST as delimiters\n");
        shell_print("  -s           Paste one file at a time instead of in parallel\n");
        shell_print("  -z           Use NUL as line delimiter\n");
        shell_print("\nRead from stdin if FILE is '-' or omitted.\n");
        shell_last_exit_code = 1;
        return;
    }
    shell_print("paste: stub (multi-file merge not yet implemented)\n");
    shell_print("  requested: ");
    shell_print(args);
    shell_print("\n");
    shell_last_exit_code = 0;
}

void cmd_tr(const char *args) {
    if (!args || !*args) {
        shell_print("tr - translate or delete characters\n");
        shell_print("Usage: tr [OPTION]... SET1 [SET2]\n");
        shell_print("  -c, -C       Operate on the complement of SET1\n");
        shell_print("  -d           Delete characters in SET1, do not translate\n");
        shell_print("  -s           Squeeze runs of repeated characters\n");
        shell_print("  -t           Truncate SET1 to length of SET2\n");
        shell_print("\nSET syntax uses character classes (e.g. [:alpha:], [:space:]).\n");
        shell_print("Escapes: \\NNN octal, \\\\ backslash, \\a/b/f/n/r/t/v.\n");
        shell_last_exit_code = 1;
        return;
    }
    shell_print("tr: stub (translate/squeeze not yet implemented)\n");
    shell_print("  requested: ");
    shell_print(args);
    shell_print("\n");
    shell_last_exit_code = 0;
}

void cmd_rev(const char *file) {
    if (!file || !*file) {
        shell_print("rev - reverse lines of a file\n");
        shell_print("Usage: rev [FILE]\n");
        shell_print("  Reverses the order of characters on each line.\n");
        shell_print("  If FILE is omitted, read from stdin.\n");
        shell_last_exit_code = 1;
        return;
    }
    shell_print("rev: stub (file not yet read)\n");
    shell_print("  requested: ");
    shell_print(file);
    shell_print("\n");
    shell_last_exit_code = 0;
}

void cmd_fold(const char *args) {
    if (!args || !*args) {
        shell_print("fold - wrap each line to a maximum width\n");
        shell_print("Usage: fold [OPTION]... [FILE]...\n");
        shell_print("  -b, --bytes    Count bytes rather than columns\n");
        shell_print("  -s             Break at spaces (don't break inside words)\n");
        shell_print("  -w WIDTH       Use WIDTH columns (default: 80)\n");
        shell_last_exit_code = 1;
        return;
    }
    shell_print("fold: stub (line wrapping not yet implemented)\n");
    shell_print("  requested: ");
    shell_print(args);
    shell_print("\n");
    shell_last_exit_code = 0;
}

void cmd_expand(const char *args) {
    if (!args || !*args) {
        shell_print("expand - convert tabs to spaces\n");
        shell_print("Usage: expand [OPTION]... [FILE]...\n");
        shell_print("  -i, --initial    Only convert leading tabs on each line\n");
        shell_print("  -t N, --tabs=N   Tab stops are every N columns (default: 8)\n");
        shell_print("  -t LIST          Use comma-separated list of tab stops\n");
        shell_last_exit_code = 1;
        return;
    }
    shell_print("expand: stub (file not yet read)\n");
    shell_print("  requested: ");
    shell_print(args);
    shell_print("\n");
    shell_print("  Use 'unexpand' to convert spaces back to tabs.\n");
    shell_last_exit_code = 0;
}

void cmd_unexpand(const char *args) {
    if (!args || !*args) {
        shell_print("unexpand - convert spaces to tabs\n");
        shell_print("Usage: unexpand [OPTION]... [FILE]...\n");
        shell_print("  -a, --all        Convert all runs of spaces (not just leading)\n");
        shell_print("  --first-only     Convert only leading sequences (default)\n");
        shell_print("  -t N, --tabs=N   Tab stops are every N columns (default: 8)\n");
        shell_last_exit_code = 1;
        return;
    }
    shell_print("unexpand: stub (file not yet read)\n");
    shell_print("  requested: ");
    shell_print(args);
    shell_print("\n");
    shell_print("  Use 'expand' to convert tabs back to spaces.\n");
    shell_last_exit_code = 0;
}

void cmd_nl(const char *file) {
    if (!file || !*file) {
        shell_print("nl - number lines of a file\n");
        shell_print("Usage: nl [OPTION]... [FILE]\n");
        shell_print("  -b STYLE        Style of numbering (a=all, t=non-empty, n=none)\n");
        shell_print("  -i N            Increment line counter by N (default: 1)\n");
        shell_print("  -n FORMAT       Number format (ln=left, rn=right, rz=right zero)\n");
        shell_print("  -s STRING       Add STRING after each number (default: TAB)\n");
        shell_print("  -v N            Start numbering at N (default: 1)\n");
        shell_print("  -w N            Width of line numbers (default: 6)\n");
        shell_last_exit_code = 1;
        return;
    }
    shell_print("nl: stub (file not yet read)\n");
    shell_print("  requested: ");
    shell_print(file);
    shell_print("\n");
    shell_print("  Use 'cat -n' for a simpler line-numbered dump.\n");
    shell_last_exit_code = 0;
}

void cmd_look(const char *args) {
    if (!args || !*args) {
        shell_print("look - display lines beginning with a given string\n");
        shell_print("Usage: look [OPTION]... STRING [FILE]\n");
        shell_print("  -a              Use the alternate dictionary (currently one file)\n");
        shell_print("  -d              Compare only letters, digits and blanks\n");
        shell_print("  -f              Ignore case differences\n");
        shell_print("  -t CHAR         Stop comparing at character CHAR\n");
        shell_print("\nIf FILE is omitted, /usr/share/dict/words is searched.\n");
        shell_last_exit_code = 1;
        return;
    }
    shell_print("look: stub (prefix match not yet implemented)\n");
    shell_print("  requested: ");
    shell_print(args);
    shell_print("\n");
    shell_print("  Use 'grep ^PREFIX file' for shell-based prefix search.\n");
    shell_last_exit_code = 0;
}

void cmd_comm(const char *args) {
    if (!args || !*args) {
        shell_print("comm - compare two sorted files line by line\n");
        shell_print("Usage: comm [OPTION]... FILE1 FILE2\n");
        shell_print("  -1    Suppress lines unique to FILE1\n");
        shell_print("  -2    Suppress lines unique to FILE2\n");
        shell_print("  -3    Suppress lines that appear in both files\n");
        shell_print("  --check-order    Check that input is correctly sorted\n");
        shell_print("  --nocheck-order  Do not check input order (default)\n");
        shell_print("\nOutput is three columns: FILE1-only, FILE2-only, both.\n");
        shell_last_exit_code = 1;
        return;
    }
    shell_print("comm: stub (comparison not yet implemented)\n");
    shell_print("  requested: ");
    shell_print(args);
    shell_print("\n");
    shell_last_exit_code = 0;
}

void cmd_tsort(const char *file) {
    if (!file || !*file) {
        shell_print("tsort - perform a topological sort\n");
        shell_print("Usage: tsort [OPTION] [FILE]\n");
        shell_print("  Reads pairs of whitespace-separated tokens describing\n");
        shell_print("  a directed edge: 'a b' means a precedes b.\n");
        shell_print("  Output: one token per line, in a valid topological order.\n");
        shell_print("\n  If FILE is omitted, read from stdin.\n");
        shell_last_exit_code = 1;
        return;
    }
    shell_print("tsort: stub (topological sort not yet implemented)\n");
    shell_print("  requested: ");
    shell_print(file);
    shell_print("\n");
    shell_last_exit_code = 0;
}
