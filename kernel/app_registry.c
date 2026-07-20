#include "app_registry.h"
#include "kheap.h"
#include "string.h"
#include "stdio.h"
#include "klog.h"
#include "gui_apps.h"
#include "games.h"
#include "app_utils.h"
#include "more_apps.h"

static void launcher_snake(void) { snake_game_run(); }
static void launcher_2048(void) { game_2048_run(); }
static void launcher_tetris(void) { tetris_run(); }
static void launcher_minesweeper(void) { minesweeper_run(); }
static void launcher_pong(void) { pong_run(); }
static void launcher_games(void) { game_menu_run(); }
static void launcher_acalc(void) { calc_interactive_run(); }
static void launcher_sysmon(void) { sysmon_run(); }
static void launcher_cal(void) { cal_run(); }
static void launcher_clock(void) { clock_run(); }
static void launcher_matrix(void) { matrix_run(); }
static void launcher_life(void) { life_run(); }
static void launcher_sokoban(void) { sokoban_run(); }
static void launcher_typing(void) { typing_run(); }
static void launcher_ascii(void) { ascii_table_run(); }
static void launcher_dmesg(void) { dmesg_run(); }
static void launcher_uname(void) { uname_run(); }
static void launcher_nano(void) { nano_run(NULL); }
static void launcher_top(void) { top_run(); }
static void launcher_cowsay(void) { cowsay_run("Hello from FunsOS!"); }
static void launcher_fortune(void) { fortune_run(); }
static void launcher_primes(void) { primes_run("100"); }
static void launcher_banner(void) { banner_run("FUNSOS"); }
static void launcher_mktemp(void) { mktemp_run(); }
static void launcher_rain(void) { rain_run(); }
static void launcher_snow(void) { snow_run(); }
static void launcher_fire(void) { fire_run(); }
static void launcher_rot13(void) { rot13_run("Uryyb, Jbeyq!"); }
static void launcher_reset(void) { reset_run(); }
static void launcher_settings(void) { gui_app_settings(); }
static void launcher_filemanager(void) { gui_app_filemanager(); }
static void launcher_sysinfo(void) { gui_app_sysinfo(); }
static void launcher_calc(void) { gui_app_calculator(); }
static void launcher_paint(void) { gui_app_paint(); }
static void launcher_notepad(void) { gui_app_notepad(); }
static void launcher_terminal(void) { gui_app_terminal(); }
static void launcher_applist(void) { gui_app_applist(); }

static app_entry_t g_apps[APP_MAX_COUNT];
static int g_app_count = 0;

void app_registry_init(void) {
    for (int i = 0; i < APP_MAX_COUNT; i++) {
        g_apps[i].name = 0;
        g_apps[i].description = 0;
        g_apps[i].launch = 0;
        g_apps[i].builtin = 0;
        g_apps[i].enabled = 0;
    }
    g_app_count = 0;

    klog_info("App registry initialized");

    app_register("settings", "System settings panel", APP_TYPE_SYSTEM, launcher_settings, 1);
    app_register("filemgr", "File manager", APP_TYPE_SYSTEM, launcher_filemanager, 1);
    app_register("sysinfo", "System information viewer", APP_TYPE_SYSTEM, launcher_sysinfo, 1);
    app_register("apps", "Application launcher", APP_TYPE_SYSTEM, launcher_applist, 1);
    app_register("terminal", "Terminal", APP_TYPE_SYSTEM, launcher_terminal, 1);
    app_register("sysmon", "System monitor", APP_TYPE_SYSTEM, launcher_sysmon, 1);
    app_register("calc", "Simple calculator", APP_TYPE_UTILITY, launcher_calc, 1);
    app_register("acalc", "Advanced calculator (expression parser)", APP_TYPE_UTILITY, launcher_acalc, 1);
    app_register("notepad", "Text editor", APP_TYPE_UTILITY, launcher_notepad, 1);
    app_register("paint", "Paint program", APP_TYPE_UTILITY, launcher_paint, 1);
    app_register("cal", "Calendar viewer", APP_TYPE_UTILITY, launcher_cal, 1);
    app_register("clock", "Digital clock", APP_TYPE_UTILITY, launcher_clock, 1);
    app_register("matrix", "Matrix rain screensaver", APP_TYPE_UTILITY, launcher_matrix, 1);
    app_register("games", "Game menu (Snake/2048/Tetris/Minesweeper/Pong)", APP_TYPE_GAME, launcher_games, 1);
    app_register("snake", "Snake game (text mode)", APP_TYPE_GAME, launcher_snake, 1);
    app_register("2048", "2048 puzzle game (text mode)", APP_TYPE_GAME, launcher_2048, 1);
    app_register("tetris", "Tetris game (text mode)", APP_TYPE_GAME, launcher_tetris, 1);
    app_register("mine", "Minesweeper game (text mode)", APP_TYPE_GAME, launcher_minesweeper, 1);
    app_register("minesweeper", "Minesweeper game (text mode)", APP_TYPE_GAME, launcher_minesweeper, 1);
    app_register("pong", "Pong game (text mode, vs CPU/2P)", APP_TYPE_GAME, launcher_pong, 1);
    app_register("life", "Conway's Game of Life", APP_TYPE_GAME, launcher_life, 1);
    app_register("sokoban", "Sokoban puzzle game", APP_TYPE_GAME, launcher_sokoban, 1);
    app_register("typing", "Typing practice", APP_TYPE_GAME, launcher_typing, 1);

    app_register("nano", "Text editor", APP_TYPE_UTILITY, launcher_nano, 1);
    app_register("ascii", "ASCII table viewer", APP_TYPE_UTILITY, launcher_ascii, 1);
    app_register("dmesg", "Kernel log viewer", APP_TYPE_SYSTEM, launcher_dmesg, 1);
    app_register("uname", "System information", APP_TYPE_SYSTEM, launcher_uname, 1);
    app_register("top", "Task monitor", APP_TYPE_SYSTEM, launcher_top, 1);
    app_register("mktemp", "Create temporary file", APP_TYPE_UTILITY, launcher_mktemp, 1);
    app_register("cowsay", "Talking cow (ASCII art)", APP_TYPE_GAME, launcher_cowsay, 1);
    app_register("fortune", "Random fortune/quote", APP_TYPE_GAME, launcher_fortune, 1);
    app_register("primes", "Prime number generator", APP_TYPE_UTILITY, launcher_primes, 1);
    app_register("banner", "Large ASCII banner text", APP_TYPE_UTILITY, launcher_banner, 1);
    app_register("rain", "Matrix digital rain effect", APP_TYPE_GAME, launcher_rain, 1);
    app_register("snow", "Falling snow animation", APP_TYPE_GAME, launcher_snow, 1);
    app_register("fire", "Fire effect animation", APP_TYPE_GAME, launcher_fire, 1);
    app_register("rot13", "ROT13 text encoder/decoder", APP_TYPE_UTILITY, launcher_rot13, 1);
    app_register("reset", "Reset terminal", APP_TYPE_SYSTEM, launcher_reset, 1);

    klog_info("Registered %d built-in applications", g_app_count);
}

int app_register(const char *name, const char *desc, app_type_t type, void (*launch)(void), int builtin) {
    if (!name || !launch) return -1;
    if (g_app_count >= APP_MAX_COUNT) return -1;

    for (int i = 0; i < g_app_count; i++) {
        if (g_apps[i].name && strcmp(g_apps[i].name, name) == 0) {
            return -1;
        }
    }

    int idx = g_app_count;
    g_apps[idx].name = name;
    g_apps[idx].description = desc;
    g_apps[idx].type = type;
    g_apps[idx].launch = launch;
    g_apps[idx].builtin = (uint8_t)builtin;
    g_apps[idx].enabled = 1;
    g_app_count++;

    klog_debug("App registered: %s (type=%d, builtin=%d)", name, type, builtin);
    return 0;
}

int app_unregister(const char *name) {
    if (!name) return -1;

    for (int i = 0; i < g_app_count; i++) {
        if (g_apps[i].name && strcmp(g_apps[i].name, name) == 0) {
            for (int j = i; j < g_app_count - 1; j++) {
                g_apps[j] = g_apps[j + 1];
            }
            g_apps[g_app_count - 1].name = 0;
            g_app_count--;
            klog_debug("App unregistered: %s", name);
            return 0;
        }
    }
    return -1;
}

app_entry_t *app_find(const char *name) {
    if (!name) return 0;

    for (int i = 0; i < g_app_count; i++) {
        if (g_apps[i].name && g_apps[i].enabled &&
            strcmp(g_apps[i].name, name) == 0) {
            return &g_apps[i];
        }
    }
    return 0;
}

app_entry_t *app_get_by_index(int index) {
    if (index < 0 || index >= g_app_count) return 0;
    return &g_apps[index];
}

int app_get_count(void) {
    return g_app_count;
}

int app_launch(const char *name) {
    app_entry_t *app = app_find(name);
    if (!app) {
        klog_warn("App not found: %s", name);
        return -1;
    }

    if (!app->launch) {
        klog_err("App has no launch function: %s", name);
        return -1;
    }

    klog_info("Launching app: %s", name);
    app->launch();
    return 0;
}

void app_list_all(void) {
    printf("Installed applications (%d):\n", g_app_count);
    for (int i = 0; i < g_app_count; i++) {
        if (g_apps[i].name && g_apps[i].enabled) {
            printf("  [%d] %-16s %s\n", i, g_apps[i].name,
                    g_apps[i].description ? g_apps[i].description : "");
        }
    }
    printf("\nGame commands:\n");
    printf("  games   - Game selection menu\n");
    printf("  snake   - Classic Snake game\n");
    printf("  2048    - 2048 puzzle game\n");
    printf("  tetris  - Tetris\n");
    printf("  mine    - Minesweeper\n");
    printf("  pong    - Pong (vs CPU or 2 players)\n");
}
