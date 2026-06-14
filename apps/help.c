#include "user_syscall.h"
#include "string.h"

int main(int argc, char *argv[])
{
    const char *help_text =
        "Available commands:\n"
        "  shell    - Command-line shell\n"
        "  ls       - List directory contents\n"
        "  cat      - Display file contents\n"
        "  echo     - Print text\n"
        "  mkdir    - Create directory\n"
        "  rm       - Remove file\n"
        "  cp       - Copy file\n"
        "  mv       - Move/rename file\n"
        "  touch    - Create empty file\n"
        "  help     - Show this help\n"
        "  calc     - Calculator\n"
        "  desktop  - Desktop environment\n"
        "  terminal - Graphical terminal\n"
        "  filemgr  - File manager\n"
        "  notepad  - Text editor\n"
        "  paint    - Drawing program\n"
        "  snake    - Snake game\n";

    sys_write(1, help_text, strlen(help_text));
    sys_exit(0);
    return 0;
}
