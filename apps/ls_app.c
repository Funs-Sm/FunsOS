#include "stdio.h"
#include "string.h"
#include "vfs.h"
#include "path.h"
#include "dentry.h"

int app_ls_main(int argc, char *argv[]) {
    const char *path = ".";
    if (argc > 1 && argv[1]) {
        path = argv[1];
    }

    dentry_t *dir = 0;
    if (path_resolve(path, &dir) != 0 || !dir) {
        printf("ls: cannot access '%s'\n", path);
        return 1;
    }

    if (!dir->inode || !(dir->inode->mode & FILE_MODE_DIR)) {
        printf("ls: '%s' is not a directory\n", path);
        return 1;
    }

    dentry_t *child = dir->child;
    while (child) {
        if (child->inode && (child->inode->mode & FILE_MODE_DIR)) {
            printf("%s/\n", child->name);
        } else {
            printf("%s\n", child->name);
        }
        child = child->next_sibling;
    }

    return 0;
}
