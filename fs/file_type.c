#include "file_type.h"
#include "string.h"
#include "vfs.h"
#include "vga_text.h"
#include "stddef.h"
#include "stdlib.h"
#include "../kernel/permission.h"

static const file_type_entry_t extension_map[] = {
    { ".c",      FT_SOURCE_C,    "C source code",       VGA_COLOR_GREEN,       VGA_COLOR_BLACK },
    { ".h",      FT_HEADER,      "C header file",       VGA_COLOR_YELLOW,      VGA_COLOR_BLACK },
    { ".cpp",    FT_SOURCE_CPP,  "C++ source code",     VGA_COLOR_GREEN,       VGA_COLOR_BLACK },
    { ".cc",     FT_SOURCE_CPP,  "C++ source code",     VGA_COLOR_GREEN,       VGA_COLOR_BLACK },
    { ".cxx",    FT_SOURCE_CPP,  "C++ source code",     VGA_COLOR_GREEN,       VGA_COLOR_BLACK },
    { ".hpp",    FT_HEADER,      "C++ header file",     VGA_COLOR_YELLOW,      VGA_COLOR_BLACK },
    { ".hxx",    FT_HEADER,      "C++ header file",     VGA_COLOR_YELLOW,      VGA_COLOR_BLACK },
    { ".s",      FT_ASM,         "Assembly source",     VGA_COLOR_CYAN,        VGA_COLOR_BLACK },
    { ".S",      FT_ASM,         "Assembly source",     VGA_COLOR_CYAN,        VGA_COLOR_BLACK },
    { ".asm",    FT_ASM,         "Assembly source",     VGA_COLOR_CYAN,        VGA_COLOR_BLACK },
    { ".py",     FT_PYTHON,      "Python script",       VGA_COLOR_GREEN,       VGA_COLOR_BLACK },
    { ".sh",     FT_SHELL,       "Shell script",        VGA_COLOR_GREEN,       VGA_COLOR_BLACK },
    { ".bash",   FT_SHELL,       "Bash script",         VGA_COLOR_GREEN,       VGA_COLOR_BLACK },
    { "Makefile",FT_MAKEFILE,    "Makefile",            VGA_COLOR_YELLOW,      VGA_COLOR_BLACK },
    { ".mk",     FT_MAKEFILE,    "Makefile",            VGA_COLOR_YELLOW,      VGA_COLOR_BLACK },
    { ".html",   FT_HTML,        "HTML document",       VGA_COLOR_MAGENTA,     VGA_COLOR_BLACK },
    { ".htm",    FT_HTML,        "HTML document",       VGA_COLOR_MAGENTA,     VGA_COLOR_BLACK },
    { ".css",    FT_CSS,         "CSS stylesheet",      VGA_COLOR_MAGENTA,     VGA_COLOR_BLACK },
    { ".js",     FT_JS,          "JavaScript file",     VGA_COLOR_YELLOW,      VGA_COLOR_BLACK },
    { ".json",   FT_JSON,        "JSON data",           VGA_COLOR_CYAN,        VGA_COLOR_BLACK },
    { ".xml",    FT_XML,         "XML document",        VGA_COLOR_MAGENTA,     VGA_COLOR_BLACK },
    { ".md",     FT_MARKDOWN,    "Markdown document",   VGA_COLOR_WHITE,       VGA_COLOR_BLACK },
    { ".txt",    FT_TEXT,        "Text file",           VGA_COLOR_WHITE,       VGA_COLOR_BLACK },
    { ".text",   FT_TEXT,        "Text file",           VGA_COLOR_WHITE,       VGA_COLOR_BLACK },
    { ".log",    FT_TEXT,        "Log file",            VGA_COLOR_LIGHT_GREY,  VGA_COLOR_BLACK },
    { ".cfg",    FT_CONFIG,      "Configuration file",  VGA_COLOR_CYAN,        VGA_COLOR_BLACK },
    { ".conf",   FT_CONFIG,      "Configuration file",  VGA_COLOR_CYAN,        VGA_COLOR_BLACK },
    { ".ini",    FT_CONFIG,      "INI config file",     VGA_COLOR_CYAN,        VGA_COLOR_BLACK },
    { ".yaml",   FT_CONFIG,      "YAML config file",    VGA_COLOR_CYAN,        VGA_COLOR_BLACK },
    { ".yml",    FT_CONFIG,      "YAML config file",    VGA_COLOR_CYAN,        VGA_COLOR_BLACK },
    { ".toml",   FT_CONFIG,      "TOML config file",    VGA_COLOR_CYAN,        VGA_COLOR_BLACK },
    { ".bmp",    FT_IMAGE,       "BMP image",           VGA_COLOR_LIGHT_MAGENTA, VGA_COLOR_BLACK },
    { ".png",    FT_IMAGE,       "PNG image",           VGA_COLOR_LIGHT_MAGENTA, VGA_COLOR_BLACK },
    { ".jpg",    FT_IMAGE,       "JPEG image",          VGA_COLOR_LIGHT_MAGENTA, VGA_COLOR_BLACK },
    { ".jpeg",   FT_IMAGE,       "JPEG image",          VGA_COLOR_LIGHT_MAGENTA, VGA_COLOR_BLACK },
    { ".gif",    FT_IMAGE,       "GIF image",           VGA_COLOR_LIGHT_MAGENTA, VGA_COLOR_BLACK },
    { ".ico",    FT_IMAGE,       "Icon file",           VGA_COLOR_LIGHT_MAGENTA, VGA_COLOR_BLACK },
    { ".pcx",    FT_IMAGE,       "PCX image",           VGA_COLOR_LIGHT_MAGENTA, VGA_COLOR_BLACK },
    { ".tga",    FT_IMAGE,       "TGA image",           VGA_COLOR_LIGHT_MAGENTA, VGA_COLOR_BLACK },
    { ".wav",    FT_AUDIO,       "WAV audio",           VGA_COLOR_LIGHT_RED,   VGA_COLOR_BLACK },
    { ".mp3",    FT_AUDIO,       "MP3 audio",           VGA_COLOR_LIGHT_RED,   VGA_COLOR_BLACK },
    { ".ogg",    FT_AUDIO,       "OGG audio",           VGA_COLOR_LIGHT_RED,   VGA_COLOR_BLACK },
    { ".flac",   FT_AUDIO,       "FLAC audio",          VGA_COLOR_LIGHT_RED,   VGA_COLOR_BLACK },
    { ".mid",    FT_AUDIO,       "MIDI audio",          VGA_COLOR_LIGHT_RED,   VGA_COLOR_BLACK },
    { ".avi",    FT_VIDEO,       "AVI video",           VGA_COLOR_LIGHT_MAGENTA, VGA_COLOR_BLACK },
    { ".mp4",    FT_VIDEO,       "MP4 video",           VGA_COLOR_LIGHT_MAGENTA, VGA_COLOR_BLACK },
    { ".mkv",    FT_VIDEO,       "MKV video",           VGA_COLOR_LIGHT_MAGENTA, VGA_COLOR_BLACK },
    { ".mov",    FT_VIDEO,       "QuickTime video",     VGA_COLOR_LIGHT_MAGENTA, VGA_COLOR_BLACK },
    { ".zip",    FT_ARCHIVE,     "ZIP archive",         VGA_COLOR_RED,         VGA_COLOR_BLACK },
    { ".tar",    FT_ARCHIVE,     "TAR archive",         VGA_COLOR_RED,         VGA_COLOR_BLACK },
    { ".gz",     FT_ARCHIVE,     "GZIP compressed",     VGA_COLOR_RED,         VGA_COLOR_BLACK },
    { ".bz2",    FT_ARCHIVE,     "BZIP2 compressed",    VGA_COLOR_RED,         VGA_COLOR_BLACK },
    { ".xz",     FT_ARCHIVE,     "XZ compressed",       VGA_COLOR_RED,         VGA_COLOR_BLACK },
    { ".7z",     FT_ARCHIVE,     "7-Zip archive",       VGA_COLOR_RED,         VGA_COLOR_BLACK },
    { ".rar",    FT_ARCHIVE,     "RAR archive",         VGA_COLOR_RED,         VGA_COLOR_BLACK },
    { ".tgz",    FT_ARCHIVE,     "Tar/GZ archive",      VGA_COLOR_RED,         VGA_COLOR_BLACK },
    { ".tbz2",   FT_ARCHIVE,     "Tar/BZ2 archive",     VGA_COLOR_RED,         VGA_COLOR_BLACK },
    { ".elf",    FT_EXECUTABLE,  "ELF executable",      VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK },
    { ".bin",    FT_EXECUTABLE,  "Binary file",         VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK },
    { ".exe",    FT_EXECUTABLE,  "Windows executable",  VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK },
    { ".com",    FT_EXECUTABLE,  "COM executable",      VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK },
    { ".fun",    FT_EXECUTABLE,  "FUN executable",      VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK },
    { ".so",     FT_LIBRARY,     "Shared library",      VGA_COLOR_LIGHT_CYAN,  VGA_COLOR_BLACK },
    { ".dll",    FT_LIBRARY,     "DLL library",         VGA_COLOR_LIGHT_CYAN,  VGA_COLOR_BLACK },
    { ".a",      FT_LIBRARY,     "Static library",      VGA_COLOR_LIGHT_CYAN,  VGA_COLOR_BLACK },
    { ".lib",    FT_LIBRARY,     "Static library",      VGA_COLOR_LIGHT_CYAN,  VGA_COLOR_BLACK },
    { ".o",      FT_LIBRARY,     "Object file",         VGA_COLOR_DARK_GREY,   VGA_COLOR_BLACK },
    { ".obj",    FT_LIBRARY,     "Object file",         VGA_COLOR_DARK_GREY,   VGA_COLOR_BLACK },
    { ".pdf",    FT_PDF,         "PDF document",        VGA_COLOR_LIGHT_RED,   VGA_COLOR_BLACK },
    { ".doc",    FT_DOCUMENT,    "Word document",       VGA_COLOR_LIGHT_BLUE,  VGA_COLOR_BLACK },
    { ".docx",   FT_DOCUMENT,    "Word document",       VGA_COLOR_LIGHT_BLUE,  VGA_COLOR_BLACK },
    { ".xls",    FT_DOCUMENT,    "Excel spreadsheet",   VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK },
    { ".xlsx",   FT_DOCUMENT,    "Excel spreadsheet",   VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK },
    { ".ppt",    FT_DOCUMENT,    "PowerPoint",          VGA_COLOR_LIGHT_RED,   VGA_COLOR_BLACK },
    { ".pptx",   FT_DOCUMENT,    "PowerPoint",          VGA_COLOR_LIGHT_RED,   VGA_COLOR_BLACK },
    { ".csv",    FT_DOCUMENT,    "CSV data",            VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK },
    { "~",       FT_BACKUP,      "Backup file",         VGA_COLOR_DARK_GREY,   VGA_COLOR_BLACK },
    { ".bak",    FT_BACKUP,      "Backup file",         VGA_COLOR_DARK_GREY,   VGA_COLOR_BLACK },
    { ".old",    FT_BACKUP,      "Old file",            VGA_COLOR_DARK_GREY,   VGA_COLOR_BLACK },
    { ".orig",   FT_BACKUP,      "Original file",       VGA_COLOR_DARK_GREY,   VGA_COLOR_BLACK },
    { ".swp",    FT_BACKUP,      "Vim swap file",       VGA_COLOR_DARK_GREY,   VGA_COLOR_BLACK },
    { ".tmp",    FT_BACKUP,      "Temporary file",      VGA_COLOR_DARK_GREY,   VGA_COLOR_BLACK },
    { NULL,      FT_UNKNOWN,     "Unknown file type",   VGA_COLOR_LIGHT_GREY,  VGA_COLOR_BLACK }
};

static uint8_t color_fg_map[FT_UNKNOWN + 1];
static uint8_t color_bg_map[FT_UNKNOWN + 1];
static int color_enabled = 0;

static const char *type_short_names[] = {
    "dir", "file", "txt", "c", "cpp", "h", "asm", "py", "sh", "make",
    "html", "css", "js", "json", "xml", "md", "cfg", "img", "audio",
    "video", "archive", "exe", "lib", "doc", "pdf", "lnk", "dev",
    "fifo", "sock", "hidden", "bak", "unknown"
};

file_type_t file_type_from_name(const char *name) {
    if (!name || !*name) return FT_UNKNOWN;
    for (int i = 0; i <= FT_UNKNOWN; i++) {
        if (strcmp(name, type_short_names[i]) == 0) {
            return (file_type_t)i;
        }
    }
    if (strcmp(name, "directory") == 0) return FT_DIR;
    if (strcmp(name, "text") == 0) return FT_TEXT;
    if (strcmp(name, "source") == 0) return FT_SOURCE_C;
    if (strcmp(name, "header") == 0) return FT_HEADER;
    if (strcmp(name, "image") == 0 || strcmp(name, "pic") == 0) return FT_IMAGE;
    if (strcmp(name, "sound") == 0) return FT_AUDIO;
    if (strcmp(name, "movie") == 0) return FT_VIDEO;
    if (strcmp(name, "zip") == 0) return FT_ARCHIVE;
    if (strcmp(name, "exec") == 0) return FT_EXECUTABLE;
    if (strcmp(name, "link") == 0 || strcmp(name, "symlink") == 0) return FT_SYMLINK;
    return FT_UNKNOWN;
}

const char *file_type_short_name(file_type_t type) {
    if (type < 0 || type > FT_UNKNOWN) return "unknown";
    return type_short_names[type];
}

void file_color_init(void) {
    file_color_reset_default();
    color_enabled = 0;
}

int file_color_enabled(void) {
    return color_enabled;
}

void file_color_enable(int enable) {
    color_enabled = enable ? 1 : 0;
}

void file_color_set(file_type_t type, uint8_t fg, uint8_t bg) {
    if (type < 0 || type > FT_UNKNOWN) return;
    color_fg_map[type] = fg & 0x0F;
    color_bg_map[type] = bg & 0x0F;
}

void file_color_reset_classic(void) {
    for (int i = 0; i <= FT_UNKNOWN; i++) {
        color_fg_map[i] = VGA_COLOR_WHITE;
        color_bg_map[i] = VGA_COLOR_BLACK;
    }
    color_enabled = 1;
}

void file_color_reset_default(void) {
    const file_type_entry_t *entry = &extension_map[0];
    while (entry->extension) {
        color_fg_map[entry->type] = entry->color_fg;
        color_bg_map[entry->type] = entry->color_bg;
        entry++;
    }
    color_fg_map[FT_DIR] = VGA_COLOR_LIGHT_BLUE;
    color_bg_map[FT_DIR] = VGA_COLOR_BLACK;
    color_fg_map[FT_SYMLINK] = VGA_COLOR_LIGHT_CYAN;
    color_bg_map[FT_SYMLINK] = VGA_COLOR_BLACK;
    color_fg_map[FT_EXECUTABLE] = VGA_COLOR_LIGHT_GREEN;
    color_bg_map[FT_EXECUTABLE] = VGA_COLOR_BLACK;
    color_fg_map[FT_REGULAR] = VGA_COLOR_LIGHT_GREY;
    color_bg_map[FT_REGULAR] = VGA_COLOR_BLACK;
    color_fg_map[FT_UNKNOWN] = VGA_COLOR_LIGHT_GREY;
    color_bg_map[FT_UNKNOWN] = VGA_COLOR_BLACK;
    color_fg_map[FT_HIDDEN] = VGA_COLOR_DARK_GREY;
    color_bg_map[FT_HIDDEN] = VGA_COLOR_BLACK;
    color_fg_map[FT_DEVICE] = VGA_COLOR_YELLOW;
    color_bg_map[FT_DEVICE] = VGA_COLOR_BLACK;
    color_enabled = 1;
}

file_type_t file_type_detect(const char *filename, uint32_t mode) {
    if (!filename || !*filename) return FT_UNKNOWN;

    if (mode & FILE_MODE_DIR) return FT_DIR;
    if (mode & FILE_MODE_LNK) return FT_SYMLINK;
    if (mode & PERM_EXEC) return FT_EXECUTABLE;

    if (filename[0] == '.') return FT_HIDDEN;

    size_t len = strlen(filename);
    if (len > 0 && filename[len - 1] == '~') return FT_BACKUP;

    const file_type_entry_t *entry = &extension_map[0];
    while (entry->extension) {
        size_t ext_len = strlen(entry->extension);
        if (ext_len == 0) { entry++; continue; }

        if (entry->extension[0] != '.') {
            if (strcmp(filename, entry->extension) == 0) {
                return entry->type;
            }
        } else {
            if (len >= ext_len &&
                strcmp(filename + len - ext_len, entry->extension) == 0) {
                return entry->type;
            }
        }
        entry++;
    }

    return FT_REGULAR;
}

const char *file_type_name(file_type_t type) {
    switch (type) {
        case FT_DIR:        return "directory";
        case FT_REGULAR:    return "regular file";
        case FT_TEXT:       return "text file";
        case FT_SOURCE_C:   return "C source";
        case FT_SOURCE_CPP: return "C++ source";
        case FT_HEADER:     return "C header";
        case FT_ASM:        return "assembly";
        case FT_PYTHON:     return "Python script";
        case FT_SHELL:      return "shell script";
        case FT_MAKEFILE:   return "makefile";
        case FT_HTML:       return "HTML document";
        case FT_CSS:        return "CSS stylesheet";
        case FT_JS:         return "JavaScript";
        case FT_JSON:       return "JSON";
        case FT_XML:        return "XML";
        case FT_MARKDOWN:   return "Markdown";
        case FT_CONFIG:     return "config file";
        case FT_IMAGE:      return "image";
        case FT_AUDIO:      return "audio";
        case FT_VIDEO:      return "video";
        case FT_ARCHIVE:    return "archive";
        case FT_EXECUTABLE: return "executable";
        case FT_LIBRARY:    return "library";
        case FT_DOCUMENT:   return "document";
        case FT_PDF:        return "PDF";
        case FT_SYMLINK:    return "symlink";
        case FT_DEVICE:     return "device";
        case FT_FIFO:       return "FIFO";
        case FT_SOCKET:     return "socket";
        case FT_HIDDEN:     return "hidden file";
        case FT_BACKUP:     return "backup file";
        default:            return "unknown";
    }
}

const char *file_type_description(file_type_t type) {
    const file_type_entry_t *entry = &extension_map[0];
    while (entry->extension) {
        if (entry->type == type) return entry->description;
        entry++;
    }
    return file_type_name(type);
}

void file_type_get_color(file_type_t type, uint8_t *fg, uint8_t *bg) {
    if (!color_enabled) {
        if (fg) *fg = VGA_COLOR_LIGHT_GREY;
        if (bg) *bg = VGA_COLOR_BLACK;
        return;
    }
    if (type < 0 || type > FT_UNKNOWN) type = FT_UNKNOWN;
    if (fg) *fg = color_fg_map[type];
    if (bg) *bg = color_bg_map[type];
}
