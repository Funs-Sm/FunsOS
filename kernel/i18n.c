#include "i18n.h"
#include "../lib/string.h"
#include "klog.h"

static i18n_lang_t g_langs[I18N_MAX_LANGS];
static int g_lang_count = 0;
static int g_current_lang = 0;

static i18n_msg_t zh_cn_messages[] = {
    {"SYS_WELCOME",         "欢迎使用 FunOS 操作系统"},
    {"SYS_VERSION",         "系统版本"},
    {"SYS_BOOTING",         "正在启动系统..."},
    {"SYS_READY",           "系统已就绪"},
    {"SYS_SHUTDOWN",        "正在关闭系统..."},
    {"SYS_REBOOT",          "正在重启系统..."},
    {"SYS_HALT",            "系统已停止"},
    {"SYS_ERROR",           "系统错误"},
    {"SYS_OUT_OF_MEMORY",   "内存不足"},
    {"SYS_FILE_NOT_FOUND",  "文件未找到"},
    {"SYS_PERMISSION_DENIED","权限被拒绝"},
    {"SYS_INVALID_ARG",     "无效参数"},
    {"SYS_DEVICE_NOT_FOUND","设备未找到"},
    {"SYS_OPERATION_FAILED","操作失败"},
    {"SYS_OPERATION_SUCCESS","操作成功"},
    {"SYS_LOADING",         "正在加载..."},
    {"SYS_SAVING",          "正在保存..."},
    {"SYS_DELETING",        "正在删除..."},
    {"SYS_CONFIRM",         "确认"},
    {"SYS_CANCEL",          "取消"},
    {"SYS_YES",             "是"},
    {"SYS_NO",              "否"},
    {"SYS_OK",              "确定"},
    {"SYS_HELP",            "帮助"},
    {"SYS_EXIT",            "退出"},
    {"SYS_SAVE",            "保存"},
    {"SYS_OPEN",            "打开"},
    {"SYS_CLOSE",           "关闭"},
    {"SYS_NEW",             "新建"},
    {"SYS_EDIT",            "编辑"},
    {"SYS_DELETE",          "删除"},
    {"SYS_INSERT",          "插入"},
    {"SYS_LINE",            "行"},
    {"SYS_COLUMN",          "列"},
    {"SYS_FILENAME",        "文件名"},
    {"SYS_SIZE",            "大小"},
    {"SYS_DATE",            "日期"},
    {"SYS_TIME",            "时间"},
    {"SYS_USER",            "用户"},
    {"SYS_PASSWORD",        "密码"},
    {"SYS_LOGIN",           "登录"},
    {"SYS_LOGOUT",          "注销"},
    {"SYS_COMMAND",         "命令"},
    {"SYS_USAGE",           "用法"},
    {"SYS_EXAMPLE",         "示例"},
    {"SYS_ERROR_OCCURRED",  "发生错误"},
    {"SYS_TRY_AGAIN",       "请重试"},
    {"SYS_PRESS_ANY_KEY",   "按任意键继续..."},
    {"SYS_UNKNOWN_COMMAND", "未知命令"},
    {"SYS_TOO_MANY_ARGS",   "参数过多"},
    {NULL, NULL}
};

static i18n_msg_t en_us_messages[] = {
    {"SYS_WELCOME",         "Welcome to FunOS"},
    {"SYS_VERSION",         "System Version"},
    {"SYS_BOOTING",         "Booting system..."},
    {"SYS_READY",           "System is ready"},
    {"SYS_SHUTDOWN",        "Shutting down system..."},
    {"SYS_REBOOT",          "Rebooting system..."},
    {"SYS_HALT",            "System halted"},
    {"SYS_ERROR",           "System Error"},
    {"SYS_OUT_OF_MEMORY",   "Out of memory"},
    {"SYS_FILE_NOT_FOUND",  "File not found"},
    {"SYS_PERMISSION_DENIED","Permission denied"},
    {"SYS_INVALID_ARG",     "Invalid argument"},
    {"SYS_DEVICE_NOT_FOUND","Device not found"},
    {"SYS_OPERATION_FAILED","Operation failed"},
    {"SYS_OPERATION_SUCCESS","Operation successful"},
    {"SYS_LOADING",         "Loading..."},
    {"SYS_SAVING",          "Saving..."},
    {"SYS_DELETING",        "Deleting..."},
    {"SYS_CONFIRM",         "Confirm"},
    {"SYS_CANCEL",          "Cancel"},
    {"SYS_YES",             "Yes"},
    {"SYS_NO",              "No"},
    {"SYS_OK",              "OK"},
    {"SYS_HELP",            "Help"},
    {"SYS_EXIT",            "Exit"},
    {"SYS_SAVE",            "Save"},
    {"SYS_OPEN",            "Open"},
    {"SYS_CLOSE",           "Close"},
    {"SYS_NEW",             "New"},
    {"SYS_EDIT",            "Edit"},
    {"SYS_DELETE",          "Delete"},
    {"SYS_INSERT",          "Insert"},
    {"SYS_LINE",            "Line"},
    {"SYS_COLUMN",          "Column"},
    {"SYS_FILENAME",        "Filename"},
    {"SYS_SIZE",            "Size"},
    {"SYS_DATE",            "Date"},
    {"SYS_TIME",            "Time"},
    {"SYS_USER",            "User"},
    {"SYS_PASSWORD",        "Password"},
    {"SYS_LOGIN",           "Login"},
    {"SYS_LOGOUT",          "Logout"},
    {"SYS_COMMAND",         "Command"},
    {"SYS_USAGE",           "Usage"},
    {"SYS_EXAMPLE",         "Example"},
    {"SYS_ERROR_OCCURRED",  "An error occurred"},
    {"SYS_TRY_AGAIN",       "Please try again"},
    {"SYS_PRESS_ANY_KEY",   "Press any key to continue..."},
    {"SYS_UNKNOWN_COMMAND", "Unknown command"},
    {"SYS_TOO_MANY_ARGS",   "Too many arguments"},
    {NULL, NULL}
};

static int count_messages(i18n_msg_t *msgs) {
    int count = 0;
    if (!msgs) return 0;
    while (msgs[count].key != NULL && msgs[count].value != NULL) {
        count++;
    }
    return count;
}

int i18n_add_language(const char *name, i18n_msg_t *msgs, int count) {
    if (!name || !msgs || g_lang_count >= I18N_MAX_LANGS) {
        return -1;
    }

    strncpy(g_langs[g_lang_count].name, name, 15);
    g_langs[g_lang_count].name[15] = '\0';
    g_langs[g_lang_count].messages = msgs;
    g_langs[g_lang_count].msg_count = count;
    g_lang_count++;

    klog_info("i18n: added language '%s' with %d messages", name, count);
    return 0;
}

int i18n_set_lang(const char *lang_name) {
    if (!lang_name) return -1;

    for (int i = 0; i < g_lang_count; i++) {
        if (strcmp(g_langs[i].name, lang_name) == 0) {
            g_current_lang = i;
            klog_info("i18n: language set to '%s'", lang_name);
            return 0;
        }
    }

    klog_warn("i18n: language '%s' not found", lang_name);
    return -1;
}

const char *i18n_get_lang(void) {
    if (g_current_lang >= 0 && g_current_lang < g_lang_count) {
        return g_langs[g_current_lang].name;
    }
    return "";
}

const char *i18n_gettext(const char *key) {
    if (!key) return "";

    if (g_current_lang < 0 || g_current_lang >= g_lang_count) {
        return key;
    }

    i18n_lang_t *lang = &g_langs[g_current_lang];
    for (int i = 0; i < lang->msg_count; i++) {
        if (lang->messages[i].key && strcmp(lang->messages[i].key, key) == 0) {
            return lang->messages[i].value;
        }
    }

    return key;
}

const char *i18n_ngettext(const char *key, int n) {
    (void)n;
    return i18n_gettext(key);
}

int i18n_get_lang_count(void) {
    return g_lang_count;
}

const char *i18n_get_lang_name(int index) {
    if (index < 0 || index >= g_lang_count) {
        return "";
    }
    return g_langs[index].name;
}

void i18n_init(void) {
    int zh_count, en_count;

    klog_info("i18n: initializing internationalization subsystem");

    g_lang_count = 0;
    g_current_lang = 0;

    zh_count = count_messages(zh_cn_messages);
    i18n_add_language(LANG_NAME_ZH_CN, zh_cn_messages, zh_count);

    en_count = count_messages(en_us_messages);
    i18n_add_language(LANG_NAME_EN_US, en_us_messages, en_count);

    i18n_set_lang(LANG_NAME_ZH_CN);

    klog_info("i18n: initialized with %d languages, default: %s",
              g_lang_count, i18n_get_lang());
}
