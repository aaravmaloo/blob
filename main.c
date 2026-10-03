// blob.c

#define _DEFAULT_SOURCE

#define BLOB_VERSION "1.5.0"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifdef _WIN32
#include <conio.h>
#include <direct.h>
#include <io.h>
#include <process.h>
#include <windows.h>
#define mkdir(path, mode) _mkdir(path)
#define unlink _unlink
#define PATH_SEP "\\"
#else
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#define PATH_SEP "/"
#endif

#ifdef _WIN32
#ifndef S_IFDIR
#define S_IFDIR _S_IFDIR
#endif
#ifndef S_IFREG
#define S_IFREG _S_IFREG
#endif
#define access _access
#endif

#ifdef _WIN32
#define STAT_ISDIR(mode) (((mode) & S_IFDIR) != 0)
#define STAT_ISREG(mode) (((mode) & S_IFREG) != 0)
#else
#define STAT_ISDIR(mode) S_ISDIR(mode)
#define STAT_ISREG(mode) S_ISREG(mode)
#endif

#define INPUT_MAX 512
#define SEARCH_MAX 128
#define TITLE_MAX 256
#define INITIAL_NOTES_CAP 32
#define VISIBLE_NOTES ((size_t)g_visible_notes)
#define DEFAULT_VISIBLE_NOTES 12
#define MAX_UNDO 10
#define QUICK_VIEW_LINES 12

typedef enum {
    UNDO_NONE = 0,
    UNDO_TRASH,
    UNDO_RENAME
} UndoType;

typedef struct {
    UndoType type;
    char current_path[PATH_MAX];
    char target_path[PATH_MAX];
    char title[TITLE_MAX];
} UndoAction;

typedef enum {
    SORT_MTIME = 0,
    SORT_TITLE,
    SORT_SIZE
} SortMode;

static SortMode g_sort_mode = SORT_MTIME;
static bool g_sort_reverse = false;

typedef enum {
    COLOR_MODE_AUTO = 0,
    COLOR_MODE_TRUE,
    COLOR_MODE_256
} ColorMode;

typedef enum {
    PLUGIN_SOURCE_LOCAL = 0,
    PLUGIN_SOURCE_ASK,
    PLUGIN_SOURCE_GITHUB
} PluginSource;

// Mirrors of config settings read in places that do not receive the config
static int g_visible_notes = DEFAULT_VISIBLE_NOTES;
static bool g_date_absolute = false;
static bool g_plugin_confirm_run = true;

#define ANSI_RESET "\x1b[0m"
#define ANSI_BOLD "\x1b[1m"
#define ANSI_DIM "\x1b[2m"
#define ANSI_RED "\x1b[31m"
#define ANSI_HIDE_CURSOR "\x1b[?25l"
#define ANSI_SHOW_CURSOR "\x1b[?25h"
#define ANSI_CLEAR_LINE "\x1b[2K"

typedef struct {
    char title[32];
    char selected[32];
    char search[32];
    char help[32];
    char status[32];
    char timestamp[32];
    char pagination[32];
    char star[32];
} Theme;

static Theme g_theme = {
    .title = "\x1b[39m",
    .selected = "\x1b[36m",
    .search = "\x1b[35m",
    .help = "\x1b[2m",
    .status = "\x1b[31m",
    .timestamp = "\x1b[90m",
    .pagination = "\x1b[90m",
    .star = "\x1b[33m",
};

void enter_alt_screen(void) {
    printf("\033[?1049h\033[H");
    fflush(stdout);
}

void exit_alt_screen(void) {
    printf("\033[?1049l");
    fflush(stdout);
}

typedef struct {
    char data_dir[PATH_MAX];
    char notes_dir[PATH_MAX];
    char addons_dir[PATH_MAX];
    char config_path[PATH_MAX];
    char favorites_path[PATH_MAX];
    char editor[INPUT_MAX];
    char theme_name[64];
    char sort_order[16];
    int purge_days;
    bool sort_reverse;
    bool restore_session;
    int visible_notes;
    bool date_absolute;
    ColorMode color_mode;
    bool show_hints;
    bool confirm_trash;
    bool open_after_create;
    PluginSource plugin_source;
    bool plugin_confirm_run;
    bool plugin_confirm_install;
    bool plugin_scan_cwd;
    char plugin_repo[128];
    char plugin_branch[64];
    // Configurable keybindings (defaults set in init_paths)
    char key_create;
    char key_rename;
    char key_trash;
    char key_trash_bin;
    char key_copy;
    char key_star;
    char key_search;
    char key_cmd;
    char key_plugins;
    char key_quit;
    char key_undo;
    char key_move_down;
    char key_move_up;
    char key_view;
} AppConfig;

typedef struct {
    char title[TITLE_MAX];
    char filename[TITLE_MAX];
    char path[PATH_MAX];
    time_t mtime;
    long size;
    bool is_favorite;
} Note;

typedef struct {
    Note *items;
    size_t count;
    size_t capacity;
} NoteList;

typedef struct {
    NoteList notes;
    size_t selected;
    char search[SEARCH_MAX];
    bool search_mode;
    bool running;
    bool show_help_expanded;
    size_t rendered_lines;
    char status[INPUT_MAX];
    UndoAction undo_stack[MAX_UNDO];
    size_t undo_count;
    UndoAction redo_stack[MAX_UNDO];
    size_t redo_count;
} AppState;

typedef enum {
    KEY_NONE = 0,
    KEY_CHAR,
    KEY_ENTER,
    KEY_ESCAPE,
    KEY_BACKSPACE,
    KEY_UP,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_CTRL_P,
    KEY_CTRL_R,
    KEY_CTRL_C,
    KEY_CTRL_S,
    KEY_CTRL_D,
    KEY_CTRL_K,
    KEY_CTRL_O,
    KEY_CTRL_T,
    KEY_CTRL_U,
    KEY_CTRL_Z,
    KEY_HOME,
    KEY_END,
    KEY_PGUP,
    KEY_PGDN,
    KEY_BACKTAB
} KeyType;

typedef struct {
    KeyType type;
    char ch;
} KeyEvent;

#ifdef _WIN32
static DWORD original_input_mode;
static HANDLE stdin_handle;
static HANDLE stdout_handle;
#else
static struct termios original_termios;
#endif

static bool raw_enabled = false;

static void note_list_free(NoteList *list) {
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static bool note_list_push(NoteList *list, const Note *note) {
    if (list->count == list->capacity) {
        size_t next_capacity = list->capacity ? list->capacity * 2 : INITIAL_NOTES_CAP;
        Note *next = realloc(list->items, next_capacity * sizeof(*next));
        if (!next) {
            return false;
        }

        list->items = next;
        list->capacity = next_capacity;
    }

    list->items[list->count++] = *note;
    return true;
}

static void push_undo(AppState *state, UndoType type, const char *current_path,
                      const char *target_path, const char *title) {
    if (state->undo_count >= MAX_UNDO) {
        // Drop the oldest action
        memmove(&state->undo_stack[0], &state->undo_stack[1],
                (MAX_UNDO - 1) * sizeof(UndoAction));
        state->undo_count = MAX_UNDO - 1;
    }
    UndoAction *a = &state->undo_stack[state->undo_count++];
    a->type = type;
    snprintf(a->current_path, sizeof(a->current_path), "%s", current_path);
    snprintf(a->target_path, sizeof(a->target_path), "%s", target_path);
    snprintf(a->title, sizeof(a->title), "%s", title);
    // Any new action invalidates the redo history
    state->redo_count = 0;
}

static void disable_raw_mode(void) {
    if (!raw_enabled) {
        return;
    }

#ifdef _WIN32
    SetConsoleMode(stdin_handle, original_input_mode);
#else
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios);
#endif

    printf(ANSI_SHOW_CURSOR ANSI_RESET);
    fflush(stdout);
    raw_enabled = false;
}

static void enable_raw_mode(void) {
    if (raw_enabled) {
        return;
    }

#ifdef _WIN32
    stdin_handle = GetStdHandle(STD_INPUT_HANDLE);
    stdout_handle = GetStdHandle(STD_OUTPUT_HANDLE);

    DWORD output_mode = 0;
    if (GetConsoleMode(stdout_handle, &output_mode)) {
        output_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(stdout_handle, output_mode);
    }

    GetConsoleMode(stdin_handle, &original_input_mode);

    DWORD raw = original_input_mode;
    raw &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT);
    raw |= ENABLE_PROCESSED_INPUT;
    SetConsoleMode(stdin_handle, raw);
#else
    tcgetattr(STDIN_FILENO, &original_termios);

    struct termios raw = original_termios;
    raw.c_iflag &= (tcflag_t)~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    raw.c_oflag &= (tcflag_t)~(OPOST);
    raw.c_lflag &= (tcflag_t)~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    raw.c_cflag &= (tcflag_t)~(PARENB);
    raw.c_cflag |= CS8;
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
#endif

    printf(ANSI_HIDE_CURSOR);
    fflush(stdout);
    raw_enabled = true;
}

#ifndef BLOB_TEST
static volatile sig_atomic_t g_signal_received = 0;

static void signal_handler(int sig) {
    (void)sig;
    if (g_signal_received) return;
    g_signal_received = 1;

    // Async-signal-safe terminal cleanup: only use write() and tcsetattr()
    static const char restore[] = "\x1b[?25h\x1b[0m\x1b[?1049l";
    write(STDOUT_FILENO, restore, sizeof(restore) - 1);

#ifndef _WIN32
    // Restore original terminal settings (async-signal-safe)
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios);
#endif

    _exit(128 + sig);
}

static void restore_terminal_at_exit(void) {
    disable_raw_mode();
}

static void setup_signal_handlers(void) {
#ifndef _WIN32
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
#else
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
#endif
}
#endif

static KeyEvent read_key(void) {
    KeyEvent key = {KEY_NONE, 0};

#ifdef _WIN32
    int c = _getch();

    if (c == 13) {
        key.type = KEY_ENTER;
        return key;
    }
    if (c == 27) {
        key.type = KEY_ESCAPE;
        return key;
    }
    if (c == 8 || c == 127) {
        key.type = KEY_BACKSPACE;
        return key;
    }
    if (c == 16) {
        key.type = KEY_CTRL_P;
        return key;
    }
    if (c == 18) {
        key.type = KEY_CTRL_R;
        return key;
    }
    if (c == 3) {
        key.type = KEY_CTRL_C;
        return key;
    }
    if (c == 19) {
        key.type = KEY_CTRL_S;
        return key;
    }
    if (c == 4) {
        key.type = KEY_CTRL_D;
        return key;
    }
    if (c == 11) {
        key.type = KEY_CTRL_K;
        return key;
    }
    if (c == 15) {
        key.type = KEY_CTRL_O;
        return key;
    }
    if (c == 20) {
        key.type = KEY_CTRL_T;
        return key;
    }
    if (c == 26) {
        key.type = KEY_CTRL_Z;
        return key;
    }
    if (c == 0 || c == 224) {
        int ext = _getch();
        if (ext == 72) key.type = KEY_UP;
        else if (ext == 80) key.type = KEY_DOWN;
        else if (ext == 75) key.type = KEY_LEFT;
        else if (ext == 77) key.type = KEY_RIGHT;
        else if (ext == 71) key.type = KEY_HOME;
        else if (ext == 79) key.type = KEY_END;
        else if (ext == 73) key.type = KEY_PGUP;
        else if (ext == 81) key.type = KEY_PGDN;
        else if (ext == 15) key.type = KEY_BACKTAB;
        return key;
    }

    key.type = KEY_CHAR;
    key.ch = (char)c;
    return key;
#else
    char c = 0;
    if (read(STDIN_FILENO, &c, 1) != 1) {
        return key;
    }

    if (c == '\r' || c == '\n') {
        key.type = KEY_ENTER;
        return key;
    }
    if (c == 127 || c == '\b') {
        key.type = KEY_BACKSPACE;
        return key;
    }
    if (c == 16) {
        key.type = KEY_CTRL_P;
        return key;
    }
    if (c == 18) {
        key.type = KEY_CTRL_R;
        return key;
    }
    if (c == 3) {
        key.type = KEY_CTRL_C;
        return key;
    }
    if (c == 19) {
        key.type = KEY_CTRL_S;
        return key;
    }
    if (c == 4) {
        key.type = KEY_CTRL_D;
        return key;
    }
    if (c == 11) {
        key.type = KEY_CTRL_K;
        return key;
    }
    if (c == 15) {
        key.type = KEY_CTRL_O;
        return key;
    }
    if (c == 20) {
        key.type = KEY_CTRL_T;
        return key;
    }
    if (c == 21) {
        key.type = KEY_CTRL_U;
        return key;
    }
    if (c == 26) {
        key.type = KEY_CTRL_Z;
        return key;
    }
    if (c == '\x1b') {
        char seq[2];

        fd_set set;
        struct timeval timeout;
        FD_ZERO(&set);
        FD_SET(STDIN_FILENO, &set);
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;

        if (select(STDIN_FILENO + 1, &set, NULL, NULL, &timeout) <= 0 ||
            read(STDIN_FILENO, &seq[0], 1) != 1 || seq[0] == '\x1b') {
            key.type = KEY_ESCAPE;
            return key;
        }

        FD_ZERO(&set);
        FD_SET(STDIN_FILENO, &set);
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;

        if (select(STDIN_FILENO + 1, &set, NULL, NULL, &timeout) <= 0 ||
            read(STDIN_FILENO, &seq[1], 1) != 1) {
            key.type = KEY_ESCAPE;
            return key;
        }
        if (seq[0] == '[') {
            if (seq[1] == 'A') key.type = KEY_UP;
            else if (seq[1] == 'B') key.type = KEY_DOWN;
            else if (seq[1] == 'C') key.type = KEY_RIGHT;
            else if (seq[1] == 'D') key.type = KEY_LEFT;
            else if (seq[1] == 'H') key.type = KEY_HOME;
            else if (seq[1] == 'F') key.type = KEY_END;
            else if (seq[1] == 'Z') key.type = KEY_BACKTAB;
            else if (seq[1] == '5' || seq[1] == '6') {
                // PgUp/PgDn arrive as ESC [ 5 ~ / ESC [ 6 ~ (3 bytes after ESC)
                char seq2 = 0;
                FD_ZERO(&set);
                FD_SET(STDIN_FILENO, &set);
                timeout.tv_sec = 0;
                timeout.tv_usec = 100000;
                if (select(STDIN_FILENO + 1, &set, NULL, NULL, &timeout) > 0 &&
                    read(STDIN_FILENO, &seq2, 1) == 1 && seq2 == '~') {
                    key.type = (seq[1] == '5') ? KEY_PGUP : KEY_PGDN;
                }
            }
        } else if (seq[0] == 'O') {
            // Application-mode Home/End: ESC O H / ESC O F
            if (seq[1] == 'H') key.type = KEY_HOME;
            else if (seq[1] == 'F') key.type = KEY_END;
        }
        return key;
    }

    key.type = KEY_CHAR;
    key.ch = c;
    return key;
#endif
}

static void clear_owned_region(AppState *state) {
    if (state->rendered_lines == 0) {
        return;
    }

    printf("\r\x1b[%zuA\x1b[J", state->rendered_lines);
    fflush(stdout);
    state->rendered_lines = 0;
}

static bool ensure_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return STAT_ISDIR(st.st_mode);
    }

    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);
    if (len >= PATH_MAX) return false;

    // Iterate through the path and create each component
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            char sep = *p;
            *p = '\0';
            if (stat(tmp, &st) != 0) {
                if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                    // Ignore errors for components like "C:" on Windows
#ifndef _WIN32
                    *p = sep;
                    return false;
#endif
                }
            }
            *p = sep;
        }
    }

    return mkdir(tmp, 0755) == 0 || errno == EEXIST;
}

static void set_default_keybindings(AppConfig *cfg) {
    cfg->key_create = 'n';
    cfg->key_rename = 'r';
    cfg->key_trash = 'd';
    cfg->key_trash_bin = 't';
    cfg->key_copy = 'y';
    cfg->key_star = '*';
    cfg->key_search = '/';
    cfg->key_cmd = ':';
    cfg->key_plugins = 'p';
    cfg->key_quit = 'q';
    cfg->key_undo = 'u';
    cfg->key_move_down = 'j';
    cfg->key_move_up = 'k';
    cfg->key_view = 'v';
}

static void set_config_defaults(AppConfig *cfg);

#ifndef BLOB_TEST
static void init_paths(AppConfig *cfg) {
#ifdef _WIN32
    const char *local_app_data = getenv("LOCALAPPDATA");
    const char *home = getenv("USERPROFILE");
    const char *fallback_editor = "notepad";

    if (local_app_data && *local_app_data) {
        snprintf(cfg->data_dir, sizeof(cfg->data_dir), "%s\\blob", local_app_data);
    } else {
        snprintf(cfg->data_dir, sizeof(cfg->data_dir), "%s\\AppData\\Local\\blob",
                 home && *home ? home : ".");
    }
#elif defined(__APPLE__)
    const char *home = getenv("HOME");
    const char *fallback_editor = "vim";

    snprintf(cfg->data_dir, sizeof(cfg->data_dir),
             "%s/Library/Application Support/blob", home && *home ? home : ".");
#else
    const char *home = getenv("HOME");
    const char *fallback_editor = "vim";

    snprintf(cfg->data_dir, sizeof(cfg->data_dir), "%s/.local/share/blob",
             home && *home ? home : ".");
#endif

    const char *editor = getenv("EDITOR");
    snprintf(cfg->editor, sizeof(cfg->editor), "%s",
             editor && *editor ? editor : fallback_editor);
    snprintf(cfg->notes_dir, sizeof(cfg->notes_dir), "%s" PATH_SEP "notes", cfg->data_dir);
    snprintf(cfg->addons_dir, sizeof(cfg->addons_dir), "%s" PATH_SEP "addons", cfg->data_dir);
    snprintf(cfg->config_path, sizeof(cfg->config_path), "%s" PATH_SEP "config", cfg->data_dir);
    snprintf(cfg->favorites_path, sizeof(cfg->favorites_path), "%s" PATH_SEP "favorites", cfg->data_dir);

    // Default settings
    snprintf(cfg->theme_name, sizeof(cfg->theme_name), "default");
    snprintf(cfg->sort_order, sizeof(cfg->sort_order), "mtime");
    cfg->purge_days = 30;
    cfg->sort_reverse = false;
    set_config_defaults(cfg);

    set_default_keybindings(cfg);

    // Initial setup diagnostics
    if (!ensure_dir(cfg->data_dir)) {
        fprintf(stderr, "Warning: could not ensure data_dir: %s\n", cfg->data_dir);
    }
    if (!ensure_dir(cfg->notes_dir)) {
        fprintf(stderr, "Warning: could not ensure notes_dir: %s\n", cfg->notes_dir);
    }
    if (!ensure_dir(cfg->addons_dir)) {
        fprintf(stderr, "Warning: could not ensure addons_dir: %s\n", cfg->addons_dir);
    }
}
#endif

static SortMode sort_mode_from_string(const char *s) {
    if (s && strcmp(s, "title") == 0) return SORT_TITLE;
    if (s && strcmp(s, "size") == 0) return SORT_SIZE;
    return SORT_MTIME;
}

/* ── themes ──────────────────────────────────────────────────────────────── */

static void set_config_defaults(AppConfig *cfg) {
    cfg->restore_session = true;
    cfg->visible_notes = DEFAULT_VISIBLE_NOTES;
    cfg->date_absolute = false;
    cfg->color_mode = COLOR_MODE_AUTO;
    cfg->show_hints = true;
    cfg->confirm_trash = true;
    cfg->open_after_create = true;
    cfg->plugin_source = PLUGIN_SOURCE_ASK;
    cfg->plugin_confirm_run = true;
    cfg->plugin_confirm_install = true;
    cfg->plugin_scan_cwd = true;
    snprintf(cfg->plugin_repo, sizeof(cfg->plugin_repo), "aaravmaloo/blob");
    snprintf(cfg->plugin_branch, sizeof(cfg->plugin_branch), "master");
    g_visible_notes = cfg->visible_notes;
    g_date_absolute = cfg->date_absolute;
    g_plugin_confirm_run = cfg->plugin_confirm_run;
}

/* Repo and branch names end up inside shell commands, so only allow URL-safe characters */
static bool is_safe_url_part(const char *s) {
    if (!s || !*s) return false;
    for (const char *c = s; *c; c++) {
        if (!(isalnum((unsigned char)*c) || *c == '-' || *c == '_' || *c == '.' || *c == '/')) {
            return false;
        }
    }
    return strstr(s, "..") == NULL && s[0] != '/' && s[0] != '-';
}

static bool parse_bool(const char *value) {
    return strcmp(value, "true") == 0 || strcmp(value, "1") == 0 || strcmp(value, "yes") == 0 ||
           strcmp(value, "on") == 0;
}

static const char *plugin_source_to_string(PluginSource source) {
    switch (source) {
    case PLUGIN_SOURCE_LOCAL:
        return "local";
    case PLUGIN_SOURCE_GITHUB:
        return "github";
    case PLUGIN_SOURCE_ASK:
    default:
        return "ask";
    }
}

static PluginSource plugin_source_from_string(const char *s) {
    if (strcmp(s, "local") == 0) return PLUGIN_SOURCE_LOCAL;
    if (strcmp(s, "github") == 0) return PLUGIN_SOURCE_GITHUB;
    return PLUGIN_SOURCE_ASK;
}

static const char *resolve_theme_name(const char *name);

static void load_config(AppConfig *cfg) {
    FILE *f = fopen(cfg->config_path, "r");
    if (!f) return;

    char line[INPUT_MAX];
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }

        char *eq = strchr(line, '=');
        if (!eq) continue;

        *eq = '\0';
        char *key = line;
        char *value = eq + 1;

        // Trim leading/trailing whitespace
        while (*key && isspace((unsigned char)*key)) key++;
        char *kend = key + strlen(key);
        while (kend > key && isspace((unsigned char)kend[-1])) kend--;
        *kend = '\0';

        while (*value && isspace((unsigned char)*value)) value++;
        char *vend = value + strlen(value);
        while (vend > value && isspace((unsigned char)vend[-1])) vend--;
        *vend = '\0';

        if (!*key || !*value) continue;

        if (strcmp(key, "editor") == 0) {
            snprintf(cfg->editor, sizeof(cfg->editor), "%s", value);
        } else if (strcmp(key, "theme") == 0) {
            snprintf(cfg->theme_name, sizeof(cfg->theme_name), "%s", resolve_theme_name(value));
        } else if (strcmp(key, "sort") == 0) {
            snprintf(cfg->sort_order, sizeof(cfg->sort_order), "%s", value);
            g_sort_mode = sort_mode_from_string(cfg->sort_order);
        } else if (strcmp(key, "sort_reverse") == 0) {
            cfg->sort_reverse = parse_bool(value);
            g_sort_reverse = cfg->sort_reverse;
        } else if (strcmp(key, "purge_days") == 0) {
            cfg->purge_days = atoi(value);
        } else if (strcmp(key, "restore_session") == 0) {
            cfg->restore_session = parse_bool(value);
        } else if (strcmp(key, "visible_notes") == 0) {
            int n = atoi(value);
            if (n >= 3 && n <= 50) {
                cfg->visible_notes = n;
                g_visible_notes = n;
            }
        } else if (strcmp(key, "date_style") == 0) {
            cfg->date_absolute = strcmp(value, "absolute") == 0;
            g_date_absolute = cfg->date_absolute;
        } else if (strcmp(key, "colors") == 0) {
            cfg->color_mode = strcmp(value, "truecolor") == 0 ? COLOR_MODE_TRUE :
                              strcmp(value, "256") == 0 ? COLOR_MODE_256 : COLOR_MODE_AUTO;
        } else if (strcmp(key, "show_hints") == 0) {
            cfg->show_hints = parse_bool(value);
        } else if (strcmp(key, "confirm_trash") == 0) {
            cfg->confirm_trash = parse_bool(value);
        } else if (strcmp(key, "open_after_create") == 0) {
            cfg->open_after_create = parse_bool(value);
        } else if (strcmp(key, "plugin_source") == 0) {
            cfg->plugin_source = plugin_source_from_string(value);
        } else if (strcmp(key, "plugin_confirm_run") == 0) {
            cfg->plugin_confirm_run = parse_bool(value);
            g_plugin_confirm_run = cfg->plugin_confirm_run;
        } else if (strcmp(key, "plugin_confirm_install") == 0) {
            cfg->plugin_confirm_install = parse_bool(value);
        } else if (strcmp(key, "plugin_scan_cwd") == 0) {
            cfg->plugin_scan_cwd = parse_bool(value);
        } else if (strcmp(key, "plugin_repo") == 0) {
            if (is_safe_url_part(value)) {
                snprintf(cfg->plugin_repo, sizeof(cfg->plugin_repo), "%s", value);
            }
        } else if (strcmp(key, "plugin_branch") == 0) {
            if (is_safe_url_part(value)) {
                snprintf(cfg->plugin_branch, sizeof(cfg->plugin_branch), "%s", value);
            }
        } else if (strncmp(key, "key_", 4) == 0) {
            if (value[0]) {
                if (strcmp(key + 4, "create") == 0) cfg->key_create = value[0];
                else if (strcmp(key + 4, "rename") == 0) cfg->key_rename = value[0];
                else if (strcmp(key + 4, "trash") == 0) cfg->key_trash = value[0];
                else if (strcmp(key + 4, "trash_bin") == 0) cfg->key_trash_bin = value[0];
                else if (strcmp(key + 4, "copy") == 0) cfg->key_copy = value[0];
                else if (strcmp(key + 4, "star") == 0) cfg->key_star = value[0];
                else if (strcmp(key + 4, "search") == 0) cfg->key_search = value[0];
                else if (strcmp(key + 4, "cmd") == 0) cfg->key_cmd = value[0];
                else if (strcmp(key + 4, "plugins") == 0) cfg->key_plugins = value[0];
                else if (strcmp(key + 4, "quit") == 0) cfg->key_quit = value[0];
                else if (strcmp(key + 4, "undo") == 0) cfg->key_undo = value[0];
                else if (strcmp(key + 4, "move_down") == 0) cfg->key_move_down = value[0];
                else if (strcmp(key + 4, "move_up") == 0) cfg->key_move_up = value[0];
                else if (strcmp(key + 4, "view") == 0) cfg->key_view = value[0];
            }
        }
    }
    fclose(f);
}

static void save_config(const AppConfig *cfg) {
    FILE *f = fopen(cfg->config_path, "w");
    if (!f) return;

    fprintf(f, "editor = %s\n", cfg->editor);
    fprintf(f, "theme = %s\n", cfg->theme_name);
    fprintf(f, "sort = %s\n", cfg->sort_order);
    fprintf(f, "sort_reverse = %s\n", cfg->sort_reverse ? "true" : "false");
    fprintf(f, "purge_days = %d\n", cfg->purge_days);
    fprintf(f, "restore_session = %s\n", cfg->restore_session ? "true" : "false");
    fprintf(f, "visible_notes = %d\n", cfg->visible_notes);
    fprintf(f, "date_style = %s\n", cfg->date_absolute ? "absolute" : "relative");
    fprintf(f, "colors = %s\n", cfg->color_mode == COLOR_MODE_TRUE ? "truecolor" :
                               cfg->color_mode == COLOR_MODE_256 ? "256" : "auto");
    fprintf(f, "show_hints = %s\n", cfg->show_hints ? "true" : "false");
    fprintf(f, "confirm_trash = %s\n", cfg->confirm_trash ? "true" : "false");
    fprintf(f, "open_after_create = %s\n", cfg->open_after_create ? "true" : "false");
    fprintf(f, "plugin_source = %s\n", plugin_source_to_string(cfg->plugin_source));
    fprintf(f, "plugin_confirm_run = %s\n", cfg->plugin_confirm_run ? "true" : "false");
    fprintf(f, "plugin_confirm_install = %s\n", cfg->plugin_confirm_install ? "true" : "false");
    fprintf(f, "plugin_scan_cwd = %s\n", cfg->plugin_scan_cwd ? "true" : "false");
    fprintf(f, "plugin_repo = %s\n", cfg->plugin_repo);
    fprintf(f, "plugin_branch = %s\n", cfg->plugin_branch);
    fprintf(f, "key_create = %c\n", cfg->key_create);
    fprintf(f, "key_rename = %c\n", cfg->key_rename);
    fprintf(f, "key_trash = %c\n", cfg->key_trash);
    fprintf(f, "key_trash_bin = %c\n", cfg->key_trash_bin);
    fprintf(f, "key_copy = %c\n", cfg->key_copy);
    fprintf(f, "key_star = %c\n", cfg->key_star);
    fprintf(f, "key_search = %c\n", cfg->key_search);
    fprintf(f, "key_cmd = %c\n", cfg->key_cmd);
    fprintf(f, "key_plugins = %c\n", cfg->key_plugins);
    fprintf(f, "key_quit = %c\n", cfg->key_quit);
    fprintf(f, "key_undo = %c\n", cfg->key_undo);
    fprintf(f, "key_move_down = %c\n", cfg->key_move_down);
    fprintf(f, "key_move_up = %c\n", cfg->key_move_up);
    fprintf(f, "key_view = %c\n", cfg->key_view);
    fclose(f);
}

static void strip_ext(char *dst, size_t dst_size, const char *filename) {
    snprintf(dst, dst_size, "%s", filename);
    size_t len = strlen(dst);
    if (len > 3 && strcmp(dst + len - 3, ".md") == 0) {
        dst[len - 3] = '\0';
    }
}

static void load_favorites_for_list(NoteList *list, const AppConfig *cfg) {
    // Reset all favorite flags
    for (size_t i = 0; i < list->count; i++) {
        list->items[i].is_favorite = false;
    }

    FILE *f = fopen(cfg->favorites_path, "r");
    if (!f) return;

    char line[TITLE_MAX];
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        if (!len) continue;

        // Match against notes by filename
        char stem[TITLE_MAX];
        strip_ext(stem, sizeof(stem), line);
        for (size_t i = 0; i < list->count; i++) {
            char nstem[TITLE_MAX];
            strip_ext(nstem, sizeof(nstem), list->items[i].filename);
            if (strcmp(nstem, stem) == 0) {
                list->items[i].is_favorite = true;
                break;
            }
        }
    }
    fclose(f);
}



/* ── themes ──────────────────────────────────────────────────────────────── */

/* Colours are "#rrggbb" (drawn as 24-bit or the nearest of the 256 xterm colours)
   or a raw escape, which follows the terminal's own palette */
typedef struct {
    const char *name;
    const char *description;
    const char *title;
    const char *selected;
    const char *search;
    const char *help;
    const char *status;
    const char *timestamp;
    const char *star;
} ThemePreset;

static const ThemePreset s_themes[] = {
    {"default", "Your terminal's own colours", "\x1b[39m", "\x1b[36m", "\x1b[35m", "\x1b[2m", "\x1b[31m", "\x1b[90m", "\x1b[33m"},
    {"mono", "No colour, bold and dim only", "\x1b[39m", "\x1b[39m", "\x1b[39m", "\x1b[2m", "\x1b[39m", "\x1b[2m", "\x1b[39m"},
    {"catppuccin-mocha", "Dark - soft pastels on deep blue", "#cdd6f4", "#cba6f7", "#89b4fa", "#7f849c", "#f38ba8", "#7f849c", "#f9e2af"},
    {"catppuccin-latte", "Light - pastels on pale grey", "#4c4f69", "#8839ef", "#1e66f5", "#6c6f85", "#d20f39", "#7c7f93", "#e64553"},
    {"tokyo-night", "Dark - neon blue and violet", "#c0caf5", "#7aa2f7", "#bb9af7", "#737aa2", "#f7768e", "#737aa2", "#e0af68"},
    {"tokyo-night-day", "Light - Tokyo Night in daylight", "#3760bf", "#2e7de9", "#9854f1", "#68709a", "#f52a65", "#68709a", "#8c6c3e"},
    {"gruvbox-dark", "Dark - warm retro browns", "#ebdbb2", "#fe8019", "#8ec07c", "#a89984", "#fb4934", "#928374", "#fabd2f"},
    {"gruvbox-light", "Light - warm retro on cream", "#3c3836", "#af3a03", "#427b58", "#7c6f64", "#9d0006", "#7c6f64", "#b57614"},
    {"rose-pine", "Dark - muted rose and pine", "#e0def4", "#c4a7e7", "#9ccfd8", "#908caa", "#eb6f92", "#6e6a86", "#f6c177"},
    {"rose-pine-dawn", "Light - rose and pine at dawn", "#575279", "#286983", "#907aa9", "#797593", "#b4637a", "#797593", "#b4637a"},
    {"dracula", "Dark - purple and pink", "#f8f8f2", "#bd93f9", "#ff79c6", "#6272a4", "#ff5555", "#6272a4", "#f1fa8c"},
    {"alucard", "Light - Dracula's light twin", "#1f1f1f", "#644ac9", "#a3144d", "#635d97", "#cb3a2a", "#635d97", "#846e15"},
    {"solarized-dark", "Dark - classic Solarized", "#93a1a1", "#268bd2", "#2aa198", "#657b83", "#dc322f", "#657b83", "#b58900"},
    {"solarized-light", "Light - classic Solarized", "#073642", "#268bd2", "#d33682", "#657b83", "#dc322f", "#657b83", "#cb4b16"},
    {"everforest", "Dark - calm forest greens", "#d3c6aa", "#a7c080", "#7fbbb3", "#859289", "#e67e80", "#859289", "#dbbc7f"},
    {"everforest-light", "Light - forest on parchment", "#5c6a72", "#3a94c5", "#3a94c5", "#829181", "#f85552", "#829181", "#f85552"},
    {"kanagawa", "Dark - ink and wave blues", "#dcd7ba", "#7e9cd8", "#957fb8", "#727169", "#e46876", "#727169", "#e6c384"},
    {"kanagawa-lotus", "Light - ink on rice paper", "#545464", "#4d699b", "#624c83", "#716e61", "#c84053", "#716e61", "#cc6d00"},
    {"one-dark", "Dark - Atom's One Dark", "#abb2bf", "#61afef", "#c678dd", "#7f848e", "#e06c75", "#7f848e", "#e5c07b"},
    {"one-light", "Light - Atom's One Light", "#383a42", "#4078f2", "#a626a4", "#696c77", "#e45649", "#696c77", "#c18401"},
    {"nord", "Dark - arctic frost blues", "#d8dee9", "#88c0d0", "#b48ead", "#7b88a1", "#bf616a", "#7b88a1", "#ebcb8b"},
};

static const char *s_theme_aliases[][2] = {
    {"dark", "tokyo-night"},
    {"light", "catppuccin-latte"},
    {"solarized", "solarized-dark"},
};

static const size_t s_theme_count = sizeof(s_themes) / sizeof(s_themes[0]);

static const char *resolve_theme_name(const char *name) {
    for (size_t i = 0; i < sizeof(s_theme_aliases) / sizeof(s_theme_aliases[0]); i++) {
        if (strcmp(s_theme_aliases[i][0], name) == 0) {
            return s_theme_aliases[i][1];
        }
    }
    return name;
}

static bool terminal_supports_true_color(void) {
    const char *colorterm = getenv("COLORTERM");
    if (colorterm && (strstr(colorterm, "truecolor") || strstr(colorterm, "24bit"))) {
        return true;
    }
    const char *term = getenv("TERM");
    if (term && (strstr(term, "kitty") || strstr(term, "alacritty") || strstr(term, "ghostty") ||
                 strstr(term, "wezterm") || strstr(term, "direct"))) {
        return true;
    }
    const char *program = getenv("TERM_PROGRAM");
    if (program && (strcmp(program, "iTerm.app") == 0 || strcmp(program, "WezTerm") == 0 ||
                    strcmp(program, "vscode") == 0 || strcmp(program, "ghostty") == 0)) {
        return true;
    }
    return getenv("WT_SESSION") != NULL;
}

static int rgb_to_xterm256(int r, int g, int b) {
    static const int levels[6] = {0, 95, 135, 175, 215, 255};
    int idx[3];
    int rgb[3] = {r, g, b};
    for (int c = 0; c < 3; c++) {
        int best = 0;
        for (int i = 1; i < 6; i++) {
            if (abs(levels[i] - rgb[c]) < abs(levels[best] - rgb[c])) best = i;
        }
        idx[c] = best;
    }
    int cube_r = levels[idx[0]], cube_g = levels[idx[1]], cube_b = levels[idx[2]];
    long cube_dist = (long)(cube_r - r) * (cube_r - r) + (long)(cube_g - g) * (cube_g - g) + (long)(cube_b - b) * (cube_b - b);

    int avg = (r + g + b) / 3;
    int grey_i = avg < 8 ? 0 : (avg > 238 ? 23 : (avg - 8 + 5) / 10);
    int grey = 8 + grey_i * 10;
    long grey_dist = (long)(grey - r) * (grey - r) + (long)(grey - g) * (grey - g) + (long)(grey - b) * (grey - b);

    if (grey_dist < cube_dist) return 232 + grey_i;
    return 16 + 36 * idx[0] + 6 * idx[1] + idx[2];
}

static void color_to_sgr(const char *src, bool true_color, char *dst, size_t dst_size) {
    unsigned int r, g, b;
    if (src[0] == '#' && sscanf(src + 1, "%02x%02x%02x", &r, &g, &b) == 3) {
        if (true_color) {
            snprintf(dst, dst_size, "\x1b[38;2;%u;%u;%um", r, g, b);
        } else {
            snprintf(dst, dst_size, "\x1b[38;5;%dm", rgb_to_xterm256((int)r, (int)g, (int)b));
        }
        return;
    }
    snprintf(dst, dst_size, "%s", src);
}

static void load_theme(const AppConfig *cfg) {
    const char *name = resolve_theme_name(cfg->theme_name);
    bool true_color = cfg->color_mode == COLOR_MODE_TRUE ||
                      (cfg->color_mode == COLOR_MODE_AUTO && terminal_supports_true_color());

    for (size_t i = 0; i < s_theme_count; i++) {
        if (strcmp(s_themes[i].name, name) == 0) {
            const ThemePreset *t = &s_themes[i];
            color_to_sgr(t->title, true_color, g_theme.title, sizeof(g_theme.title));
            color_to_sgr(t->selected, true_color, g_theme.selected, sizeof(g_theme.selected));
            color_to_sgr(t->search, true_color, g_theme.search, sizeof(g_theme.search));
            color_to_sgr(t->help, true_color, g_theme.help, sizeof(g_theme.help));
            color_to_sgr(t->status, true_color, g_theme.status, sizeof(g_theme.status));
            color_to_sgr(t->timestamp, true_color, g_theme.timestamp, sizeof(g_theme.timestamp));
            color_to_sgr(t->timestamp, true_color, g_theme.pagination, sizeof(g_theme.pagination));
            color_to_sgr(t->star, true_color, g_theme.star, sizeof(g_theme.star));
            return;
        }
    }
    // Unknown names keep the current colours
}

#ifndef _WIN32
static bool has_md_extension(const char *name) {
    size_t len = strlen(name);
    return len > 3 && strcmp(name + len - 3, ".md") == 0;
}
#endif

static void title_from_filename(char *dst, size_t dst_size, const char *filename) {
    snprintf(dst, dst_size, "%s", filename);

    size_t len = strlen(dst);
    if (len > 3 && strcmp(dst + len - 3, ".md") == 0) {
        dst[len - 3] = '\0';
    }

    for (char *p = dst; *p; p++) {
        if (*p == '-' || *p == '_') {
            *p = ' ';
        }
    }
}

static int note_cmp(const void *a, const void *b) {
    const Note *left = a;
    const Note *right = b;

    // Favorites sort to the top
    if (left->is_favorite && !right->is_favorite) return -1;
    if (!left->is_favorite && right->is_favorite) return 1;

    int r = 0;
    switch (g_sort_mode) {
    case SORT_TITLE:
        r = strcmp(left->title, right->title);
        break;
    case SORT_SIZE:
        if (left->size < right->size) r = -1;
        else if (left->size > right->size) r = 1;
        break;
    case SORT_MTIME:
    default:
        if (left->mtime < right->mtime) r = 1;
        else if (left->mtime > right->mtime) r = -1;
        break;
    }
    if (r == 0) r = strcmp(left->title, right->title);
    return g_sort_reverse ? -r : r;
}

static bool load_notes(NoteList *list, const AppConfig *cfg) {
    note_list_free(list);

#ifdef _WIN32
    char pattern[PATH_MAX];
    snprintf(pattern, sizeof(pattern), "%s" PATH_SEP "*.md", cfg->notes_dir);

    WIN32_FIND_DATAA data;
    HANDLE find = FindFirstFileA(pattern, &data);
    if (find == INVALID_HANDLE_VALUE) {
        return true;
    }

    do {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue;
        }

        Note note;
        memset(&note, 0, sizeof(note));
        snprintf(note.filename, sizeof(note.filename), "%s", data.cFileName);
        title_from_filename(note.title, sizeof(note.title), note.filename);
        snprintf(note.path, sizeof(note.path), "%s" PATH_SEP "%s", cfg->notes_dir, note.filename);

        struct stat st;
        if (stat(note.path, &st) == 0) {
            note.mtime = st.st_mtime;
            note.size = (long)st.st_size;
        }

        if (!note_list_push(list, &note)) {
            FindClose(find);
            return false;
        }
    } while (FindNextFileA(find, &data));

    FindClose(find);
#else
    DIR *dir = opendir(cfg->notes_dir);
    if (!dir) {
        return true;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (!has_md_extension(entry->d_name)) {
            continue;
        }

        Note note;
        memset(&note, 0, sizeof(note));
        snprintf(note.filename, sizeof(note.filename), "%s", entry->d_name);
        title_from_filename(note.title, sizeof(note.title), note.filename);
        snprintf(note.path, sizeof(note.path), "%s" PATH_SEP "%s", cfg->notes_dir, entry->d_name);

        struct stat st;
        if (stat(note.path, &st) == 0 && STAT_ISREG(st.st_mode)) {
            note.mtime = st.st_mtime;
            note.size = (long)st.st_size;
            if (!note_list_push(list, &note)) {
                closedir(dir);
                return false;
            }
        }
    }

    closedir(dir);
#endif

    qsort(list->items, list->count, sizeof(*list->items), note_cmp);
    return true;
}

static void sanitize_slug(const char *title, char *slug, size_t slug_size) {
    size_t out = 0;
    bool pending_dash = false;

    for (size_t i = 0; title[i] && out + 1 < slug_size; i++) {
        unsigned char ch = (unsigned char)title[i];

        if ((ch <= 127 && isalnum(ch)) || ch >= 128) {
            if (pending_dash && out > 0 && out + 1 < slug_size) {
                slug[out++] = '-';
            }
            slug[out++] = (ch <= 127) ? (char)tolower(ch) : (char)ch;
            pending_dash = false;
        } else if (ch == '.' && out > 0) {
            pending_dash = true;
        } else if (isspace(ch) || ch == '-' || ch == '_' || ch == '/' || ch == '\\') {
            pending_dash = true;
        }
    }

    while (out > 0 && slug[out - 1] == '-') {
        out--;
    }

    if (out == 0) {
        snprintf(slug, slug_size, "untitled");
    } else {
        slug[out] = '\0';
    }
}

static void unique_note_path(const AppConfig *cfg, const char *slug, char *path, size_t path_size) {
    snprintf(path, path_size, "%s" PATH_SEP "%s.md", cfg->notes_dir, slug);

    if (access(path, 0) != 0) {
        return;
    }

    for (unsigned int i = 2; i < 10000; i++) {
        snprintf(path, path_size, "%s" PATH_SEP "%s-%u.md", cfg->notes_dir, slug, i);
        if (access(path, 0) != 0) {
            return;
        }
    }
}

static void unique_path_in_dir(const char *dir, const char *filename, char *path, size_t path_size) {
    snprintf(path, path_size, "%s" PATH_SEP "%s", dir, filename);

    if (access(path, 0) != 0) {
        return;
    }

    char stem[TITLE_MAX];
    char ext[32] = "";
    snprintf(stem, sizeof(stem), "%s", filename);

    char *dot = strrchr(stem, '.');
    if (dot && strlen(dot) < sizeof(ext)) {
        snprintf(ext, sizeof(ext), "%s", dot);
        *dot = '\0';
    }

    for (unsigned int i = 2; i < 10000; i++) {
        snprintf(path, path_size, "%s" PATH_SEP "%s-%u%s", dir, stem, i, ext);
        if (access(path, 0) != 0) {
            return;
        }
    }
}

static bool create_note_file(const AppConfig *cfg, const char *title, char *path, size_t path_size) {
    char slug[TITLE_MAX];
    sanitize_slug(title, slug, sizeof(slug));
    unique_note_path(cfg, slug, path, path_size);

    if (!ensure_dir(cfg->notes_dir)) {
        return false;
    }

    FILE *file = fopen(path, "w");
    if (!file) {
        return false;
    }

    fprintf(file, "# %s\n\n", title);
    fclose(file);
    return true;
}

static bool contains_case_insensitive(const char *haystack, const char *needle) {
    if (!needle[0]) {
        return true;
    }

    for (size_t i = 0; haystack[i]; i++) {
        size_t j = 0;
        while (needle[j] &&
               haystack[i + j] &&
               tolower((unsigned char)haystack[i + j]) == tolower((unsigned char)needle[j])) {
            j++;
        }
        if (!needle[j]) {
            return true;
        }
    }

    return false;
}

static bool note_visible(const AppState *state, size_t index) {
    return contains_case_insensitive(state->notes.items[index].title, state->search);
}

static size_t visible_count(const AppState *state) {
    size_t count = 0;
    for (size_t i = 0; i < state->notes.count; i++) {
        if (note_visible(state, i)) {
            count++;
        }
    }
    return count;
}

static bool selected_is_visible(const AppState *state) {
    return state->selected < state->notes.count && note_visible(state, state->selected);
}

static void select_first_visible(AppState *state) {
    for (size_t i = 0; i < state->notes.count; i++) {
        if (note_visible(state, i)) {
            state->selected = i;
            return;
        }
    }
    state->selected = 0;
}

static void normalize_selection(AppState *state) {
    if (state->notes.count == 0) {
        state->selected = 0;
        return;
    }

    if (state->selected >= state->notes.count) {
        state->selected = state->notes.count - 1;
    }

    if (!selected_is_visible(state)) {
        select_first_visible(state);
    }
}

static void move_selection(AppState *state, int direction) {
    if (visible_count(state) == 0) {
        return;
    }

    normalize_selection(state);

    if (direction > 0) {
        for (size_t i = state->selected + 1; i < state->notes.count; i++) {
            if (note_visible(state, i)) {
                state->selected = i;
                return;
            }
        }
    } else {
        size_t i = state->selected;
        while (i > 0) {
            i--;
            if (note_visible(state, i)) {
                state->selected = i;
                return;
            }
        }
    }
}

static size_t first_rendered_note(const AppState *state) {
    size_t visible_before_selected = 0;
    for (size_t i = 0; i < state->selected && i < state->notes.count; i++) {
        if (note_visible(state, i)) {
            visible_before_selected++;
        }
    }

    size_t skip_visible = 0;
    if (visible_before_selected >= VISIBLE_NOTES) {
        skip_visible = visible_before_selected - VISIBLE_NOTES + 1;
    }

    for (size_t i = 0; i < state->notes.count; i++) {
        if (!note_visible(state, i)) {
            continue;
        }
        if (skip_visible == 0) {
            return i;
        }
        skip_visible--;
    }

    return 0;
}

static void render_line(AppState *state, const char *text) {
    printf("\r" ANSI_CLEAR_LINE "%s\n", text);
    state->rendered_lines++;
}

static void format_relative_time(time_t mtime, char *buf, size_t buf_size);

#define TITLE_COL_MIN 16
#define TITLE_COL_MAX 48
#define TIME_COL 7
#define TIME_COL_ABSOLUTE 12

/* Copies src into dst as exactly width columns, cutting with an ellipsis or padding with spaces */
static void fit_column(char *dst, size_t dst_size, const char *src, size_t width) {
    size_t total = 0;
    for (const unsigned char *p = (const unsigned char *)src; *p; p++) {
        if ((*p & 0xC0) != 0x80) total++;
    }

    size_t keep = total <= width ? total : (width > 0 ? width - 1 : 0);
    size_t out = 0;
    size_t cols = 0;
    for (const unsigned char *p = (const unsigned char *)src; *p && out + 1 < dst_size; p++) {
        if ((*p & 0xC0) != 0x80) {
            if (cols == keep) break;
            cols++;
        }
        dst[out++] = (char)*p;
    }

    if (total > width && width > 0 && out + 4 < dst_size) {
        memcpy(dst + out, "\xe2\x80\xa6", 3);
        out += 3;
        cols++;
    }
    while (cols < width && out + 1 < dst_size) {
        dst[out++] = ' ';
        cols++;
    }
    dst[out] = '\0';
}

static const char *sort_label(void) {
    switch (g_sort_mode) {
    case SORT_TITLE:
        return g_sort_reverse ? "z-a" : "a-z";
    case SORT_SIZE:
        return g_sort_reverse ? "largest first" : "smallest first";
    case SORT_MTIME:
    default:
        return g_sort_reverse ? "oldest first" : "newest first";
    }
}

static int terminal_columns(void) {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info)) {
        return info.srWindow.Right - info.srWindow.Left + 1;
    }
#else
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        return ws.ws_col;
    }
#endif
    return 80;
}

static void append_hint(char *buf, size_t buf_size, const char *key, const char *label) {
    size_t len = strlen(buf);
    if (len >= buf_size) return;
    snprintf(buf + len, buf_size - len, "%s" ANSI_BOLD "%s" ANSI_RESET "%s %s" ANSI_RESET,
             len ? "  " : "", key, g_theme.help, label);
}

static void append_key_hint(char *buf, size_t buf_size, char key, const char *label) {
    char k[2] = {key, '\0'};
    append_hint(buf, buf_size, k, label);
}

static void start_hint_group(char *buf, size_t buf_size, const char *group) {
    snprintf(buf, buf_size, "%s%-7s" ANSI_RESET, g_theme.help, group);
}

#ifndef BLOB_TEST
static void render_plugin_keybinds_help(AppState *state, const AppConfig *cfg);
static void render_ui(AppState *state, const AppConfig *cfg) {
    normalize_selection(state);
    clear_owned_region(state);

    size_t total_visible = visible_count(state);
    size_t total_notes = state->notes.count;

    char header[256];
    if (total_visible != total_notes) {
        snprintf(header, sizeof(header),
                 ANSI_BOLD "blob" ANSI_RESET "%s v" BLOB_VERSION "  \xc2\xb7  %zu of %zu notes  \xc2\xb7  %s" ANSI_RESET,
                 g_theme.help, total_visible, total_notes, sort_label());
    } else {
        snprintf(header, sizeof(header),
                 ANSI_BOLD "blob" ANSI_RESET "%s v" BLOB_VERSION "  \xc2\xb7  %zu %s  \xc2\xb7  %s" ANSI_RESET,
                 g_theme.help, total_notes, total_notes == 1 ? "note" : "notes", sort_label());
    }
    render_line(state, header);
    render_line(state, "");

    if (state->search_mode || state->search[0]) {
        char line[SEARCH_MAX + 32];
        snprintf(line, sizeof(line), "%s/ %s%s", g_theme.search, state->search, ANSI_RESET);
        render_line(state, line);
        render_line(state, "");
    }

    int cols = terminal_columns();
    int time_w = g_date_absolute ? TIME_COL_ABSOLUTE : TIME_COL;
    int row_chrome = 6 + time_w;
    size_t title_w = cols > row_chrome + TITLE_COL_MIN ? (size_t)(cols - row_chrome - 1) : TITLE_COL_MIN;
    if (title_w > TITLE_COL_MAX) title_w = TITLE_COL_MAX;

    size_t shown = 0;

    if (total_notes == 0) {
        char line[128];
        snprintf(line, sizeof(line), "%s  no notes yet, press %c to create one%s",
                 g_theme.help, cfg->key_create, ANSI_RESET);
        render_line(state, line);
    } else if (total_visible == 0) {
        char line[64];
        snprintf(line, sizeof(line), "%s  no matching notes%s", g_theme.help, ANSI_RESET);
        render_line(state, line);
    } else {
        size_t start = first_rendered_note(state);
        for (size_t i = start; i < total_notes && shown < VISIBLE_NOTES; i++) {
            if (!note_visible(state, i)) {
                continue;
            }

            const Note *note = &state->notes.items[i];
            bool selected = i == state->selected;

            char time_buf[32];
            format_relative_time(note->mtime, time_buf, sizeof(time_buf));

            char title[TITLE_COL_MAX * 4 + 8];
            fit_column(title, sizeof(title), note->title, title_w);

            char star[32];
            if (note->is_favorite) {
                snprintf(star, sizeof(star), "%s\xe2\x98\x85" ANSI_RESET " ", g_theme.star);
            } else {
                snprintf(star, sizeof(star), "  ");
            }

            char line[sizeof(title) + 128];
            if (selected) {
                snprintf(line, sizeof(line), "%s\xe2\x96\x8c" ANSI_RESET " %s" ANSI_BOLD "%s%s" ANSI_RESET "  %s%*s" ANSI_RESET,
                         g_theme.selected, star, g_theme.selected, title,
                         g_theme.timestamp, time_w, time_buf);
            } else {
                snprintf(line, sizeof(line), "  %s%s%s" ANSI_RESET "  %s%*s" ANSI_RESET,
                         star, g_theme.title, title,
                         g_theme.timestamp, time_w, time_buf);
            }
            render_line(state, line);
            shown++;
        }
    }

    if (total_visible > VISIBLE_NOTES) {
        size_t start = first_rendered_note(state);
        size_t visible_before_start = 0;
        for (size_t i = 0; i < start && i < total_notes; i++) {
            if (note_visible(state, i)) visible_before_start++;
        }
        char page_info[64];
        snprintf(page_info, sizeof(page_info), "%s  %zu-%zu of %zu" ANSI_RESET, g_theme.pagination,
                 visible_before_start + 1, visible_before_start + shown, total_visible);
        render_line(state, page_info);
    }

    if (!state->search_mode && !state->show_help_expanded && !cfg->show_hints) {
        if (state->status[0]) {
            render_line(state, "");
            char status_line[INPUT_MAX + 16];
            snprintf(status_line, sizeof(status_line), "%s%s%s", g_theme.status, state->status, ANSI_RESET);
            render_line(state, status_line);
            state->status[0] = '\0';
        }
        fflush(stdout);
        return;
    }

    render_line(state, "");

    char hints[1024];
    char nav[8];
    snprintf(nav, sizeof(nav), "%c/%c", cfg->key_move_up, cfg->key_move_down);

    if (state->search_mode) {
        hints[0] = '\0';
        append_hint(hints, sizeof(hints), "enter", "open");
        append_hint(hints, sizeof(hints), "esc", "clear");
        append_hint(hints, sizeof(hints), "\xe2\x86\x91\xe2\x86\x93", "move");
        render_line(state, hints);
    } else if (state->show_help_expanded) {
        start_hint_group(hints, sizeof(hints), "notes");
        append_key_hint(hints, sizeof(hints), cfg->key_create, "new");
        append_key_hint(hints, sizeof(hints), cfg->key_rename, "rename");
        append_key_hint(hints, sizeof(hints), cfg->key_trash, "trash");
        append_key_hint(hints, sizeof(hints), cfg->key_copy, "copy path");
        append_key_hint(hints, sizeof(hints), cfg->key_star, "star");
        append_key_hint(hints, sizeof(hints), cfg->key_view, "view");
        render_line(state, hints);

        start_hint_group(hints, sizeof(hints), "move");
        append_hint(hints, sizeof(hints), nav, "up/down");
        append_hint(hints, sizeof(hints), "g/G", "top/bottom");
        append_hint(hints, sizeof(hints), "PgUp/PgDn", "page");
        append_hint(hints, sizeof(hints), "^U/^D", "half page");
        render_line(state, hints);

        start_hint_group(hints, sizeof(hints), "more");
        append_key_hint(hints, sizeof(hints), cfg->key_search, "search");
        append_key_hint(hints, sizeof(hints), cfg->key_trash_bin, "bin");
        append_key_hint(hints, sizeof(hints), cfg->key_undo, "undo");
        append_hint(hints, sizeof(hints), "^Z", "redo");
        append_key_hint(hints, sizeof(hints), cfg->key_cmd, "commands");
        append_key_hint(hints, sizeof(hints), cfg->key_plugins, "plugins");
        append_hint(hints, sizeof(hints), ",", "settings");
        render_line(state, hints);

        start_hint_group(hints, sizeof(hints), "ctrl");
        append_hint(hints, sizeof(hints), "^R", "reminders");
        append_hint(hints, sizeof(hints), "^K", "keybinds");
        append_hint(hints, sizeof(hints), "^T", "sort");
        append_hint(hints, sizeof(hints), "?", "less");
        append_key_hint(hints, sizeof(hints), cfg->key_quit, "quit");
        render_line(state, hints);

        render_plugin_keybinds_help(state, cfg);
    } else {
        struct { char key; const char *label; } bar[] = {
            {cfg->key_create, "new"},
            {cfg->key_rename, "rename"},
            {cfg->key_trash, "trash"},
            {cfg->key_search, "search"},
            {cfg->key_view, "view"},
            {cfg->key_cmd, "cmd"},
        };
        size_t budget = cols > 1 ? (size_t)cols - 1 : 0;
        size_t width = strlen("? help  q quit");
        hints[0] = '\0';
        for (size_t i = 0; i < sizeof(bar) / sizeof(bar[0]); i++) {
            size_t entry_width = 1 + 1 + strlen(bar[i].label) + 2;
            if (width + entry_width > budget) break;
            append_key_hint(hints, sizeof(hints), bar[i].key, bar[i].label);
            width += entry_width;
        }
        append_hint(hints, sizeof(hints), "?", "help");
        append_key_hint(hints, sizeof(hints), cfg->key_quit, "quit");
        render_line(state, hints);
    }

    if (state->status[0]) {
        render_line(state, "");
        char status_line[INPUT_MAX + 16];
        snprintf(status_line, sizeof(status_line), "%s%s%s", g_theme.status, state->status, ANSI_RESET);
        render_line(state, status_line);
        state->status[0] = '\0';
    }

    fflush(stdout);
}
#endif

static bool prompt_text(AppState *state, const char *label, char *buffer, size_t buffer_size) {
    clear_owned_region(state);

    printf("? %s  %s(enter to confirm, esc to cancel)" ANSI_RESET "\n", label, g_theme.help);
    state->rendered_lines = 1;
    printf(ANSI_SHOW_CURSOR);

    size_t len = 0;
    buffer[0] = '\0';
    bool confirmed = false;
    bool done = false;

    while (!done) {
        printf("\r" ANSI_CLEAR_LINE "> %s", buffer);
        fflush(stdout);

        KeyEvent key = read_key();
        if (key.type == KEY_ENTER) {
            confirmed = true;
            done = true;
        } else if (key.type == KEY_ESCAPE || key.type == KEY_CTRL_C) {
            done = true;
        } else if (key.type == KEY_BACKSPACE) {
            while (len > 0 && (((unsigned char)buffer[len - 1]) & 0xC0) == 0x80) {
                len--;
            }
            if (len > 0) {
                len--;
            }
            buffer[len] = '\0';
        } else if (key.type == KEY_CTRL_U) {
            len = 0;
            buffer[0] = '\0';
        } else if (key.type == KEY_CHAR && ((unsigned char)key.ch >= 32 || key.ch == '\t') && len + 1 < buffer_size) {
            buffer[len++] = key.ch == '\t' ? ' ' : key.ch;
            buffer[len] = '\0';
        }
    }

    printf(ANSI_HIDE_CURSOR "\n");
    fflush(stdout);
    state->rendered_lines = 2;

    if (!confirmed) {
        buffer[0] = '\0';
        return false;
    }
    return buffer[0] != '\0';
}

static bool prompt_confirm(AppState *state, const char *message) {
    clear_owned_region(state);
    printf("? %s (y/N)", message);
    fflush(stdout);

    KeyEvent key = read_key();
    bool confirmed = key.type == KEY_CHAR && (key.ch == 'y' || key.ch == 'Y');
    printf("\r" ANSI_CLEAR_LINE);
    fflush(stdout);
    return confirmed;
}

#ifndef _WIN32
static int split_editor_command(char *command, char **argv, size_t argv_cap) {
    size_t argc = 0;
    char *p = command;

    while (*p && argc + 1 < argv_cap) {
        while (isspace((unsigned char)*p)) {
            p++;
        }
        if (!*p) {
            break;
        }

        char quote = 0;
        if (*p == '"' || *p == '\'') {
            quote = *p++;
        }

        argv[argc++] = p;

        while (*p) {
            if (quote) {
                if (*p == quote) {
                    *p++ = '\0';
                    break;
                }
            } else if (isspace((unsigned char)*p)) {
                *p++ = '\0';
                break;
            }
            p++;
        }
    }

    argv[argc] = NULL;
    return (int)argc;
}
#endif

#ifdef _WIN32
static void append_windows_quoted_arg(char *dst, size_t dst_size, const char *arg) {
    size_t len = strlen(dst);

    if (len + 1 < dst_size) {
        dst[len++] = '"';
        dst[len] = '\0';
    }

    for (size_t i = 0; arg[i] && len + 2 < dst_size; i++) {
        if (arg[i] == '"') {
            dst[len++] = '\\';
        }
        dst[len++] = arg[i];
        dst[len] = '\0';
    }

    if (len + 1 < dst_size) {
        dst[len++] = '"';
        dst[len] = '\0';
    }
}

static void build_windows_editor_command(const AppConfig *cfg, const char *path,
                                         char *command, size_t command_size) {
    command[0] = '\0';

    if (cfg->editor[0] == '"') {
        snprintf(command, command_size, "%s ", cfg->editor);
    } else {
        append_windows_quoted_arg(command, command_size, cfg->editor);
        strncat(command, " ", command_size - strlen(command) - 1);
    }

    append_windows_quoted_arg(command, command_size, path);
}
#endif

static bool open_path_in_editor(AppState *state, const AppConfig *cfg, const char *path) {
    clear_owned_region(state);
    disable_raw_mode();

    printf("Opening in %s...\n", cfg->editor);
    fflush(stdout);

#ifdef _WIN32
    char command[PATH_MAX + INPUT_MAX + 8];
    build_windows_editor_command(cfg, path, command, sizeof(command));

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);
    bool launched = CreateProcessA(NULL, command, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi) != 0;
    int exit_code = -1;
    if (launched) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD code = 0;
        if (GetExitCodeProcess(pi.hProcess, &code)) {
            exit_code = (int)code;
        }
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
#else
    char editor[INPUT_MAX];
    snprintf(editor, sizeof(editor), "%s", cfg->editor);

    char *argv[32];
    int argc = split_editor_command(editor, argv, sizeof(argv) / sizeof(argv[0]));
    if (argc <= 0 || argc + 1 >= (int)(sizeof(argv) / sizeof(argv[0]))) {
        enable_raw_mode();
        return false;
    }
    argv[argc] = (char *)path;
    argv[argc + 1] = NULL;

    pid_t pid = fork();
    bool launched = pid >= 0;
    int exit_code = -1;

    if (pid == 0) {
        execvp(argv[0], argv);
        _exit(127);
    } else if (pid > 0) {
        int status = 0;
        while (waitpid(pid, &status, 0) == -1 && errno == EINTR) {
        }
        if (WIFEXITED(status)) {
            exit_code = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            exit_code = 128 + WTERMSIG(status);
        }
        // execvp failing in the child is reported as 127
        if (exit_code == 127) {
            launched = false;
        }
    }
#endif

    bool ok = launched && exit_code == 0;

    // Keep the editor's error output on screen instead of redrawing over it
    if (!ok) {
        if (!launched) {
            printf("\nblob: could not launch editor '%s'. Check the 'editor' setting or $EDITOR.\n",
                   cfg->editor);
            snprintf(state->status, sizeof(state->status), "failed to launch editor: %s", cfg->editor);
        } else {
            printf("\nblob: editor '%s' exited with code %d (see the error above).\n",
                   cfg->editor, exit_code);
            snprintf(state->status, sizeof(state->status), "editor exited with code %d", exit_code);
        }
        printf("Press any key to continue...");
        fflush(stdout);
        enable_raw_mode();
        read_key();
        printf("\n");
    } else {
        enable_raw_mode();
    }

    state->rendered_lines = ok ? 1 : 0;
    return ok;
}

static void create_note_flow(AppState *state, const AppConfig *cfg) {
    char title[INPUT_MAX];
    if (!prompt_text(state, "Title", title, sizeof(title))) {
        return;
    }

    char path[PATH_MAX];
    if (!create_note_file(cfg, title, path, sizeof(path))) {
        snprintf(state->status, sizeof(state->status), "failed to create note: %s", strerror(errno));
        return;
    }

    if (!cfg->open_after_create || open_path_in_editor(state, cfg, path)) {
        snprintf(state->status, sizeof(state->status), "created \"%s\"", title);
    }
    load_notes(&state->notes, cfg);
    load_favorites_for_list(&state->notes, cfg);
    normalize_selection(state);
}

static void delete_note_flow(AppState *state, const AppConfig *cfg) {
    if (state->notes.count == 0 || !selected_is_visible(state)) {
        return;
    }

    Note selected = state->notes.items[state->selected];
    char message[TITLE_MAX + 32];
    snprintf(message, sizeof(message), "Move \"%s\" to trash?", selected.title);

    if (cfg->confirm_trash && !prompt_confirm(state, message)) {
        return;
    }

    char trash_dir[PATH_MAX];
    snprintf(trash_dir, sizeof(trash_dir), "%s" PATH_SEP ".trash", cfg->notes_dir);
    if (!ensure_dir(trash_dir)) {
        snprintf(state->status, sizeof(state->status), "failed to create trash");
        return;
    }

    char trash_path[PATH_MAX];
    unique_path_in_dir(trash_dir, selected.filename, trash_path, sizeof(trash_path));

    size_t previous = state->selected;

    if (rename(selected.path, trash_path) != 0) {
        snprintf(state->status, sizeof(state->status), "failed to trash note: %s", strerror(errno));
        return;
    }

    push_undo(state, UNDO_TRASH, trash_path, selected.path, selected.title);

    snprintf(state->status, sizeof(state->status), "Trashed \"%s\" (press %c to undo)", selected.title, cfg->key_undo);

    load_notes(&state->notes, cfg);
    load_favorites_for_list(&state->notes, cfg);
    state->selected = previous;
    normalize_selection(state);
}

/* ── interactive trash viewer ───────────────────────────────────────────── */

static bool load_trash_notes(NoteList *list, const AppConfig *cfg) {
    note_list_free(list);
    char trash_dir[PATH_MAX];
    snprintf(trash_dir, sizeof(trash_dir), "%s" PATH_SEP ".trash", cfg->notes_dir);

#ifdef _WIN32
    char pattern[PATH_MAX];
    snprintf(pattern, sizeof(pattern), "%s" PATH_SEP "*.md", trash_dir);
    WIN32_FIND_DATAA data;
    HANDLE find = FindFirstFileA(pattern, &data);
    if (find == INVALID_HANDLE_VALUE) return true;
    do {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        Note note;
        memset(&note, 0, sizeof(note));
        snprintf(note.filename, sizeof(note.filename), "%s", data.cFileName);
        title_from_filename(note.title, sizeof(note.title), note.filename);
        snprintf(note.path, sizeof(note.path), "%s" PATH_SEP "%s", trash_dir, note.filename);
        struct stat st;
        if (stat(note.path, &st) == 0) {
            note.mtime = st.st_mtime;
            note.size = (long)st.st_size;
        }
        if (!note_list_push(list, &note)) { FindClose(find); return false; }
    } while (FindNextFileA(find, &data));
    FindClose(find);
#else
    DIR *dir = opendir(trash_dir);
    if (!dir) return true;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (!has_md_extension(entry->d_name)) continue;
        Note note;
        memset(&note, 0, sizeof(note));
        snprintf(note.filename, sizeof(note.filename), "%s", entry->d_name);
        title_from_filename(note.title, sizeof(note.title), note.filename);
        snprintf(note.path, sizeof(note.path), "%s" PATH_SEP "%s", trash_dir, entry->d_name);
        struct stat st;
        if (stat(note.path, &st) == 0 && STAT_ISREG(st.st_mode)) {
            note.mtime = st.st_mtime;
            note.size = (long)st.st_size;
            if (!note_list_push(list, &note)) { closedir(dir); return false; }
        }
    }
    closedir(dir);
#endif
    qsort(list->items, list->count, sizeof(*list->items), note_cmp);
    return true;
}

static int purge_old_trashed_notes(const AppConfig *cfg, int purge_days) {
    if (purge_days <= 0) return 0;

    char trash_dir[PATH_MAX];
    snprintf(trash_dir, sizeof(trash_dir), "%s" PATH_SEP ".trash", cfg->notes_dir);

    time_t now = time(NULL);
    time_t cutoff = now - (time_t)purge_days * 86400;
    int purged = 0;

#ifdef _WIN32
    char pattern[PATH_MAX];
    snprintf(pattern, sizeof(pattern), "%s" PATH_SEP "*.md", trash_dir);
    WIN32_FIND_DATAA data;
    HANDLE find = FindFirstFileA(pattern, &data);
    if (find == INVALID_HANDLE_VALUE) return 0;
    do {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s" PATH_SEP "%s", trash_dir, data.cFileName);
        struct stat st;
        if (stat(path, &st) == 0 && st.st_mtime < cutoff) {
            if (unlink(path) == 0) purged++;
        }
    } while (FindNextFileA(find, &data));
    FindClose(find);
#else
    DIR *dir = opendir(trash_dir);
    if (!dir) return 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (!has_md_extension(entry->d_name)) continue;
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s" PATH_SEP "%s", trash_dir, entry->d_name);
        struct stat st;
        if (stat(path, &st) == 0 && STAT_ISREG(st.st_mode) && st.st_mtime < cutoff) {
            if (unlink(path) == 0) purged++;
        }
    }
    closedir(dir);
#endif
    return purged;
}

#ifndef BLOB_TEST
static void render_trash_ui(AppState *state, const NoteList *trash, size_t sel) {
    clear_owned_region(state);
    render_line(state, ANSI_BOLD "blob: trash" ANSI_RESET);
    render_line(state, "");

    if (trash->count == 0) {
        render_line(state, ANSI_DIM "trash is empty" ANSI_RESET);
    } else {
        size_t start = 0;
        if (sel >= VISIBLE_NOTES) start = sel - VISIBLE_NOTES + 1;
        size_t shown = 0;
        for (size_t i = start; i < trash->count && shown < VISIBLE_NOTES; i++, shown++) {
            char time_buf[32];
            format_relative_time(trash->items[i].mtime, time_buf, sizeof(time_buf));
            char line[TITLE_MAX + 64];
            const char *prefix = (i == sel) ? g_theme.selected : "";
            snprintf(line, sizeof(line), "%s> %s%-40s%s %s%s%s",
                     prefix,
                     g_theme.title,
                     trash->items[i].title,
                     ANSI_RESET,
                     g_theme.timestamp,
                     time_buf,
                     ANSI_RESET);
            render_line(state, line);
        }
        if (trash->count > VISIBLE_NOTES) {
            char pg[64];
            snprintf(pg, sizeof(pg), "%sShowing %zu-%zu of %zu%s",
                     g_theme.pagination,
                     start + 1,
                     start + shown,
                     trash->count,
                     ANSI_RESET);
            render_line(state, pg);
        }
    }

    render_line(state, "");
    render_line(state, ANSI_DIM "────────────────────────────────" ANSI_RESET);
    render_line(state, "");

    char help[128];
    snprintf(help, sizeof(help), "%s[ENTER] restore%s", g_theme.help, ANSI_RESET);
    render_line(state, help);
    snprintf(help, sizeof(help), "%s[D] permanently delete%s", g_theme.help, ANSI_RESET);
    render_line(state, help);
    snprintf(help, sizeof(help), "%s[ESC/q] back%s", g_theme.help, ANSI_RESET);
    render_line(state, help);

    if (state->status[0]) {
        render_line(state, "");
        char sl[INPUT_MAX + 16];
        snprintf(sl, sizeof(sl), "%s%s%s", g_theme.status, state->status, ANSI_RESET);
        render_line(state, sl);
        state->status[0] = '\0';
    }
    fflush(stdout);
}

static void trash_viewer_flow(AppState *state, const AppConfig *cfg) {
    NoteList trash = {NULL, 0, 0};
    load_trash_notes(&trash, cfg);

    size_t sel = 0;
    bool running = true;

    while (running) {
        render_trash_ui(state, &trash, sel);
        KeyEvent key = read_key();

        if (key.type == KEY_UP) {
            if (sel > 0) sel--;
        } else if (key.type == KEY_DOWN) {
            if (trash.count > 0 && sel + 1 < trash.count) sel++;
        } else if (key.type == KEY_ESCAPE ||
                   (key.type == KEY_CHAR && key.ch == 'q')) {
            running = false;
        } else if (key.type == KEY_ENTER) {
            if (trash.count == 0) continue;
            Note *n = &trash.items[sel];
            char dst[PATH_MAX];
            unique_path_in_dir(cfg->notes_dir, n->filename, dst, sizeof(dst));
            if (rename(n->path, dst) != 0) {
                snprintf(state->status, sizeof(state->status),
                         "failed to restore: %s", strerror(errno));
            } else {
                snprintf(state->status, sizeof(state->status),
                         "restored \"%s\"", n->title);
                load_trash_notes(&trash, cfg);
                if (sel > 0 && sel >= trash.count) sel = trash.count > 0 ? trash.count - 1 : 0;
                load_notes(&state->notes, cfg);
                load_favorites_for_list(&state->notes, cfg);
                normalize_selection(state);
            }
        } else if (key.type == KEY_CHAR && key.ch == 'D') {
            if (trash.count == 0) continue;
            Note *n = &trash.items[sel];
            char msg[TITLE_MAX + 48];
            snprintf(msg, sizeof(msg), "Permanently delete \"%s\"?", n->title);
            if (prompt_confirm(state, msg)) {
                if (unlink(n->path) != 0) {
                    snprintf(state->status, sizeof(state->status),
                             "delete failed: %s", strerror(errno));
                } else {
                    load_trash_notes(&trash, cfg);
                    if (sel > 0 && sel >= trash.count) sel = trash.count > 0 ? trash.count - 1 : 0;
                }
            }
        }
    }

    note_list_free(&trash);
    clear_owned_region(state);
}
#endif

static void rename_note_flow(AppState *state, const AppConfig *cfg) {
    if (state->notes.count == 0 || !selected_is_visible(state)) {
        return;
    }

    Note selected = state->notes.items[state->selected];
    char new_title[INPUT_MAX];
    snprintf(new_title, sizeof(new_title), "%s", selected.title);

    if (!prompt_text(state, "New title", new_title, sizeof(new_title))) {
        return;
    }

    char new_path[PATH_MAX];
    char slug[TITLE_MAX];
    sanitize_slug(new_title, slug, sizeof(slug));
    unique_note_path(cfg, slug, new_path, sizeof(new_path));

    if (rename(selected.path, new_path) != 0) {
        snprintf(state->status, sizeof(state->status), "failed to rename note: %s", strerror(errno));
        return;
    }

    push_undo(state, UNDO_RENAME, new_path, selected.path, selected.title);

    snprintf(state->status, sizeof(state->status), "Renamed (press %c to undo)", cfg->key_undo);

    load_notes(&state->notes, cfg);
    load_favorites_for_list(&state->notes, cfg);
    normalize_selection(state);
}

static void copy_path_to_clipboard(AppState *state, const AppConfig *cfg) {
    (void)cfg;
    if (state->notes.count == 0 || !selected_is_visible(state)) {
        return;
    }

    Note selected = state->notes.items[state->selected];

#ifdef _WIN32
    clear_owned_region(state);
    disable_raw_mode();

    if (OpenClipboard(NULL)) {
        EmptyClipboard();
        HGLOBAL hGlob = GlobalAlloc(GMEM_MOVEABLE, strlen(selected.path) + 1);
        if (hGlob) {
            char *pGlob = GlobalLock(hGlob);
            strcpy(pGlob, selected.path);
            GlobalUnlock(hGlob);
            SetClipboardData(CF_TEXT, hGlob);
        }
        CloseClipboard();
        snprintf(state->status, sizeof(state->status), "Copied path to clipboard");
    } else {
        snprintf(state->status, sizeof(state->status), "Failed to open clipboard");
    }

    enable_raw_mode();
#else
    // Escape single quotes in path for safe shell usage
    char safe_path[PATH_MAX * 2];
    size_t spi = 0;
    for (size_t i = 0; selected.path[i] && spi + 4 < sizeof(safe_path); i++) {
        if (selected.path[i] == '\'') {
            safe_path[spi++] = '\'';
            safe_path[spi++] = '\\';
            safe_path[spi++] = '\'';
            safe_path[spi++] = '\'';
        } else {
            safe_path[spi++] = selected.path[i];
        }
    }
    safe_path[spi] = '\0';

    char cmd[PATH_MAX * 2 + 128];
    snprintf(cmd, sizeof(cmd),
        "printf '%%s' '%s' | xclip -selection clipboard 2>/dev/null || "
        "printf '%%s' '%s' | xsel -b 2>/dev/null || "
        "printf '%%s' '%s' | pbcopy 2>/dev/null",
        safe_path, safe_path, safe_path);
    if (system(cmd) == 0) {
        snprintf(state->status, sizeof(state->status), "Copied path to clipboard");
    } else {
        snprintf(state->status, sizeof(state->status), "Clipboard unavailable (need xclip/xsel/pbcopy)");
    }
#endif
}

static void format_relative_time(time_t mtime, char *buf, size_t buf_size) {
    if (g_date_absolute) {
        struct tm *tm = localtime(&mtime);
        strftime(buf, buf_size, "%b %d %H:%M", tm);
        return;
    }

    time_t now = time(NULL);
    long diff = (long)(now - mtime);

    if (diff < 60) {
        snprintf(buf, buf_size, "%lds ago", diff);
    } else if (diff < 3600) {
        snprintf(buf, buf_size, "%ldm ago", diff / 60);
    } else if (diff < 86400) {
        snprintf(buf, buf_size, "%ldh ago", diff / 3600);
    } else if (diff < 604800) {
        snprintf(buf, buf_size, "%ldd ago", diff / 86400);
    } else {
        struct tm *tm = localtime(&mtime);
        strftime(buf, buf_size, "%b %d", tm);
    }
}


static bool save_favorites_to_disk(const AppState *state, const AppConfig *cfg) {
    FILE *f = fopen(cfg->favorites_path, "w");
    if (!f) return false;

    for (size_t i = 0; i < state->notes.count; i++) {
        if (state->notes.items[i].is_favorite) {
            fprintf(f, "%s\n", state->notes.items[i].filename);
        }
    }
    fclose(f);
    return true;
}

static void save_session(const AppConfig *cfg, const char *note_path) {
    char session_path[PATH_MAX];
    snprintf(session_path, sizeof(session_path), "%s" PATH_SEP "session", cfg->data_dir);
    FILE *f = fopen(session_path, "w");
    if (f) {
        fprintf(f, "%s\n", note_path ? note_path : "");
        fclose(f);
    }
}

static bool load_session(const AppConfig *cfg, char *out, size_t out_size) {
    char session_path[PATH_MAX];
    snprintf(session_path, sizeof(session_path), "%s" PATH_SEP "session", cfg->data_dir);
    FILE *f = fopen(session_path, "r");
    if (!f) return false;
    bool ok = fgets(out, (int)out_size, f) != NULL;
    fclose(f);
    if (ok) {
        out[strcspn(out, "\r\n")] = '\0';
    }
    return ok && out[0] != '\0';
}

static void toggle_favorite(AppState *state, const AppConfig *cfg) {
    if (state->notes.count == 0 || !selected_is_visible(state)) return;

    size_t idx = state->selected;
    bool now_fav = !state->notes.items[idx].is_favorite;
    state->notes.items[idx].is_favorite = now_fav;
    save_favorites_to_disk(state, cfg);

    // Save the filename explicitly before sort
    char filename[TITLE_MAX];
    snprintf(filename, sizeof(filename), "%s", state->notes.items[idx].filename);
    const char *title = state->notes.items[idx].title;

    snprintf(state->status, sizeof(state->status), "%s \"%s\"", now_fav ? "Starred" : "Unstarred", title);

    // Re-sort notes since favorites may have changed
    qsort(state->notes.items, state->notes.count, sizeof(*state->notes.items), note_cmp);

    // Find the selected note again after sorting
    for (size_t i = 0; i < state->notes.count; i++) {
        if (strcmp(state->notes.items[i].filename, filename) == 0) {
            state->selected = i;
            break;
        }
    }
}

static void handle_search_key(AppState *state, KeyEvent key) {
    size_t len = strlen(state->search);

    if (key.type == KEY_ESCAPE) {
        state->search[0] = '\0';
        state->search_mode = false;
        normalize_selection(state);
        return;
    }

    if (key.type == KEY_BACKSPACE) {
        if (len > 0) {
            state->search[len - 1] = '\0';
            normalize_selection(state);
        }
        return;
    }

    if (key.type == KEY_CHAR && isprint((unsigned char)key.ch) && len + 1 < sizeof(state->search)) {
        state->search[len] = key.ch;
        state->search[len + 1] = '\0';
        normalize_selection(state);
    }
}

#define PLUGIN_NAME_MAX 64
#define PLUGIN_DESC_MAX 256
#define PLUGIN_AUTH_MAX 128
#define PLUGIN_VERSION_MAX 32
#define PLUGIN_MODE_MAX 16
#define PLUGIN_PERMS_MAX 256

typedef struct {
    char name[PLUGIN_NAME_MAX];
    char authors[PLUGIN_AUTH_MAX];
    char description[PLUGIN_DESC_MAX];
    char version[PLUGIN_VERSION_MAX];
    char mode[PLUGIN_MODE_MAX];
    char permissions[PLUGIN_PERMS_MAX];
    char keybind;
    int api;
    char dir_path[PATH_MAX];
    char c_path[PATH_MAX];
    char exe_path[PATH_MAX];
    bool is_compiled;
    bool is_remote;
    bool is_disabled;
    bool has_keybind_conflict;
    bool is_legacy;
    bool update_available;
    bool exists_on_remote;
} Plugin;

typedef struct {
    Plugin *items;
    size_t count;
    size_t capacity;
} PluginList;

static void plugin_list_free(PluginList *list) {
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static bool plugin_list_push(PluginList *list, const Plugin *plugin) {
    if (list->count == list->capacity) {
        size_t next_capacity = list->capacity ? list->capacity * 2 : 8;
        Plugin *next = realloc(list->items, next_capacity * sizeof(*next));
        if (!next) return false;
        list->items = next;
        list->capacity = next_capacity;
    }
    list->items[list->count++] = *plugin;
    return true;
}

static bool is_plugin_system_enabled(const AppConfig *cfg) {
    char state_path[PATH_MAX];
    snprintf(state_path, sizeof(state_path), "%s" PATH_SEP "plugin_state", cfg->data_dir);
    FILE *f = fopen(state_path, "r");
    if (!f) {
        return true;
    }
    char buf[32];
    if (fgets(buf, sizeof(buf), f)) {
        fclose(f);
        return strstr(buf, "disabled") == NULL;
    }
    fclose(f);
    return true;
}

static void set_plugin_system_enabled(const AppConfig *cfg, bool enabled) {
    char state_path[PATH_MAX];
    snprintf(state_path, sizeof(state_path), "%s" PATH_SEP "plugin_state", cfg->data_dir);
    FILE *f = fopen(state_path, "w");
    if (f) {
        fprintf(f, "%s", enabled ? "enabled" : "disabled");
        fclose(f);
    }
}

static bool is_plugin_disabled_on_disk(const AppConfig *cfg, const char *name) {
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s" PATH_SEP "disabled_plugins", cfg->data_dir);
    FILE *f = fopen(path, "r");
    if (!f) return false;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && isspace((unsigned char)line[len - 1])) {
            line[--len] = '\0';
        }
        if (strcmp(line, name) == 0) {
            fclose(f);
            return true;
        }
    }
    fclose(f);
    return false;
}

static void set_plugin_disabled_on_disk(const AppConfig *cfg, const char *name, bool disabled) {
    char path[PATH_MAX + 64];
    snprintf(path, sizeof(path), "%s" PATH_SEP "disabled_plugins", cfg->data_dir);

    char names[32][PLUGIN_NAME_MAX];
    size_t count = 0;
    FILE *f = fopen(path, "r");
    if (f) {
        char line[PLUGIN_NAME_MAX + 64];
        while (fgets(line, sizeof(line), f) && count < 32) {
            size_t len = strlen(line);
            while (len > 0 && isspace((unsigned char)line[len - 1])) {
                line[--len] = '\0';
            }
            if (line[0] && strcmp(line, name) != 0) {
                snprintf(names[count++], PLUGIN_NAME_MAX, "%s", line);
            }
        }
        fclose(f);
    }

    if (disabled) {
        if (count < 32) {
            snprintf(names[count++], PLUGIN_NAME_MAX, "%s", name);
        }
    }

    f = fopen(path, "w");
    if (f) {
        for (size_t i = 0; i < count; i++) {
            fprintf(f, "%s\n", names[i]);
        }
        fclose(f);
    }
}

static bool key_is_core_reserved(char key) {
    const char *reserved = "nrdty*/p:qujkgGv?,";
    return key && (strchr(reserved, key) != NULL || key == '\r' || key == '\n');
}

/* ── plugin keybind overrides ─────────────────────────────────────────── */

static char get_plugin_keybind_override(const AppConfig *cfg, const char *name) {
    char path[PATH_MAX + 64];
    snprintf(path, sizeof(path), "%s" PATH_SEP "plugin_keybinds", cfg->data_dir);
    FILE *f = fopen(path, "r");
    if (!f) return '\0';
    char line[PLUGIN_NAME_MAX + 16];
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        if (strcmp(line, name) == 0 && eq[1]) {
            fclose(f);
            return eq[1];
        }
    }
    fclose(f);
    return '\0';
}

static void set_plugin_keybind_override(const AppConfig *cfg, const char *name, char keybind) {
    char path[PATH_MAX + 64];
    snprintf(path, sizeof(path), "%s" PATH_SEP "plugin_keybinds", cfg->data_dir);

    char entries[64][PLUGIN_NAME_MAX + 4];
    size_t count = 0;

    // Read existing entries, filter out the one we're changing
    FILE *f = fopen(path, "r");
    if (f) {
        char line[PLUGIN_NAME_MAX + 16];
        while (fgets(line, sizeof(line), f) && count < 64) {
            size_t len = strlen(line);
            while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';
            char *eq = strchr(line, '=');
            if (eq) *eq = '\0';
            if (line[0] && strcmp(line, name) != 0) {
                size_t out = strlen(line);
                snprintf(line + out, sizeof(line) - out, "=%s", eq ? eq + 1 : "");
                snprintf(entries[count++], sizeof(entries[0]), "%s", line);
            }
        }
        fclose(f);
    }

    // Add or update this plugin's entry
    if (keybind && count < 64) {
        snprintf(entries[count++], sizeof(entries[0]), "%s=%c", name, keybind);
    }

    f = fopen(path, "w");
    if (f) {
        for (size_t i = 0; i < count; i++) {
            fprintf(f, "%s\n", entries[i]);
        }
        fclose(f);
    }
}

static bool plugin_uses_workspace(const Plugin *plugin) {
    return strcmp(plugin->mode, "workspace") == 0;
}

static bool parse_manifest_string(const char *line, const char *key, char *dst, size_t dst_size) {
    if (strstr(line, key) == NULL) {
        return false;
    }

    char *p = strchr(line, '=');
    if (!p) {
        return false;
    }
    p++;
    while (*p && (*p == ' ' || *p == '<' || *p == '{' || *p == '"' || *p == '\'' || *p == '[')) p++;
    char *end = p + strlen(p);
    while (end > p && (end[-1] == ' ' || end[-1] == '>' || end[-1] == '}' || end[-1] == '"' || end[-1] == '\'' || end[-1] == ']')) end--;
    *end = '\0';
    snprintf(dst, dst_size, "%s", p);
    return true;
}

/* Turns a manifest list like read-note","write-note into read-note, write-note */
static void tidy_manifest_list(char *s, size_t size) {
    char out[512];
    size_t o = 0;
    for (const char *c = s; *c && o + 3 < sizeof(out); c++) {
        if (*c == '"' || *c == '\'') continue;
        if (*c == ',') {
            out[o++] = ',';
            out[o++] = ' ';
            while (c[1] == ' ' || c[1] == '"' || c[1] == '\'') c++;
            continue;
        }
        out[o++] = *c;
    }
    out[o] = '\0';
    snprintf(s, size, "%s", out);
}

static bool parse_plugin_readme(const char *readme_path, Plugin *plugin) {
    FILE *f = fopen(readme_path, "r");
    if (!f) return false;

    char line[512];
    plugin->name[0] = '\0';
    plugin->authors[0] = '\0';
    plugin->description[0] = '\0';
    plugin->version[0] = '\0';
    plugin->permissions[0] = '\0';
    snprintf(plugin->mode, sizeof(plugin->mode), "note");
    plugin->api = 1;
    plugin->is_legacy = true;
    plugin->keybind = '\0';

    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && isspace((unsigned char)line[len - 1])) {
            line[--len] = '\0';
        }

        if (line[0] == '#' && line[1] == ' ') {
            char *name = line + 2;
            while (*name && isspace((unsigned char)*name)) name++;
            snprintf(plugin->name, sizeof(plugin->name), "%s", name);
        } else if (strstr(line, "[authors] =") != NULL) {
            parse_manifest_string(line, "[authors] =", plugin->authors, sizeof(plugin->authors));
        } else if (strstr(line, "[description] =") != NULL) {
            parse_manifest_string(line, "[description] =", plugin->description, sizeof(plugin->description));
        } else if (strstr(line, "[version] =") != NULL) {
            parse_manifest_string(line, "[version] =", plugin->version, sizeof(plugin->version));
        } else if (strstr(line, "[mode] =") != NULL) {
            parse_manifest_string(line, "[mode] =", plugin->mode, sizeof(plugin->mode));
        } else if (strstr(line, "[permissions] =") != NULL) {
            parse_manifest_string(line, "[permissions] =", plugin->permissions, sizeof(plugin->permissions));
        } else if (strstr(line, "[api] =") != NULL) {
            char *p = strchr(line, '=');
            if (p) {
                plugin->api = atoi(p + 1);
                plugin->is_legacy = false;
            }
        } else if (strstr(line, "[keybind] =") != NULL) {
            char *p = strchr(line, '=');
            if (p) {
                p++;
                while (*p && isspace((unsigned char)*p)) p++;
                if (*p) {
                    plugin->keybind = *p;
                }
            }
        }
    }
    fclose(f);
    tidy_manifest_list(plugin->authors, sizeof(plugin->authors));
    tidy_manifest_list(plugin->permissions, sizeof(plugin->permissions));
    plugin->has_keybind_conflict = key_is_core_reserved(plugin->keybind);
    return plugin->name[0] != '\0';
}

static void scan_addons_dir(PluginList *list, const AppConfig *cfg, const char *addons_base_path) {
    if (access(addons_base_path, 0) != 0) {
        return;
    }

#ifdef _WIN32
    char pattern[PATH_MAX];
    snprintf(pattern, sizeof(pattern), "%s\\*", addons_base_path);

    WIN32_FIND_DATAA data;
    HANDLE find = FindFirstFileA(pattern, &data);
    if (find == INVALID_HANDLE_VALUE) {
        return;
    }

    do {
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            continue;
        }
        if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0) {
            continue;
        }

        Plugin p;
        memset(&p, 0, sizeof(p));
        snprintf(p.name, sizeof(p.name), "%s", data.cFileName);
        snprintf(p.dir_path, sizeof(p.dir_path), "%s\\%s", addons_base_path, data.cFileName);
        snprintf(p.c_path, sizeof(p.c_path), "%s\\%s.c", p.dir_path, p.name);
        snprintf(p.exe_path, sizeof(p.exe_path), "%s\\%s.exe", p.dir_path, p.name);
        p.is_remote = false;

        char readme_path[PATH_MAX];
        snprintf(readme_path, sizeof(readme_path), "%s\\README.md", p.dir_path);

        if (parse_plugin_readme(readme_path, &p)) {
            if (access(p.exe_path, 0) == 0) {
                p.is_compiled = true;
            }
            p.is_disabled = is_plugin_disabled_on_disk(cfg, p.name);
            bool dup = false;
            for (size_t i = 0; i < list->count; i++) {
                if (strcmp(list->items[i].name, p.name) == 0) {
                    dup = true;
                    break;
                }
            }
            if (!dup) {
                plugin_list_push(list, &p);
            }
        }
    } while (FindNextFileA(find, &data));
    FindClose(find);
#else
    DIR *dir = opendir(addons_base_path);
    if (!dir) {
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", addons_base_path, entry->d_name);
        struct stat st;
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
            Plugin p;
            memset(&p, 0, sizeof(p));
            snprintf(p.name, sizeof(p.name), "%s", entry->d_name);
            snprintf(p.dir_path, sizeof(p.dir_path), "%s/%s", addons_base_path, entry->d_name);
            snprintf(p.c_path, sizeof(p.c_path), "%s/%s.c", p.dir_path, p.name);
            snprintf(p.exe_path, sizeof(p.exe_path), "%s/%s", p.dir_path, p.name);
            p.is_remote = false;

            char readme_path[PATH_MAX];
            snprintf(readme_path, sizeof(readme_path), "%s/README.md", p.dir_path);

            if (parse_plugin_readme(readme_path, &p)) {
                if (access(p.exe_path, 0) == 0) {
                    p.is_compiled = true;
                }
                p.is_disabled = is_plugin_disabled_on_disk(cfg, p.name);
                bool dup = false;
                for (size_t i = 0; i < list->count; i++) {
                    if (strcmp(list->items[i].name, p.name) == 0) {
                        dup = true;
                        break;
                    }
                }
                if (!dup) {
                    plugin_list_push(list, &p);
                }
            }
        }
    }
    closedir(dir);
#endif
}

static void mark_plugin_keybind_conflicts(PluginList *list, const AppConfig *cfg) {
    for (size_t i = 0; i < list->count; i++) {
        // Check for a per-plugin keybind override first
        char override = cfg ? get_plugin_keybind_override(cfg, list->items[i].name) : '\0';
        if (override) {
            list->items[i].keybind = override;
        }
        list->items[i].has_keybind_conflict = key_is_core_reserved(list->items[i].keybind);
    }

    for (size_t i = 0; i < list->count; i++) {
        for (size_t j = i + 1; j < list->count; j++) {
            if (list->items[i].keybind &&
                list->items[i].keybind == list->items[j].keybind) {
                list->items[i].has_keybind_conflict = true;
                list->items[j].has_keybind_conflict = true;
            }
        }
    }
}

/* ── Render plugin keybinds in expanded help ── */
#ifndef BLOB_TEST
static void render_plugin_keybinds_help(AppState *state, const AppConfig *cfg) {
    PluginList exp_plugins = {NULL, 0, 0};
    scan_addons_dir(&exp_plugins, cfg, cfg->addons_dir);
    if (cfg->plugin_scan_cwd) scan_addons_dir(&exp_plugins, cfg, "addons");
    mark_plugin_keybind_conflicts(&exp_plugins, cfg);

    bool has_plugins = false;
    for (size_t i = 0; i < exp_plugins.count; i++) {
        Plugin *p = &exp_plugins.items[i];
        if (p->is_compiled && !p->is_disabled && !p->has_keybind_conflict && p->keybind) {
            has_plugins = true;
            break;
        }
    }

    if (has_plugins) {
        char plugin_line[1024];
        size_t width = 7;
        start_hint_group(plugin_line, sizeof(plugin_line), "plugins");
        for (size_t i = 0; i < exp_plugins.count; i++) {
            Plugin *p = &exp_plugins.items[i];
            if (!p->is_compiled || p->is_disabled || p->has_keybind_conflict || !p->keybind) continue;
            size_t entry_width = 2 + 2 + strlen(p->name);
            if (width > 7 && width + entry_width > 78) {
                render_line(state, plugin_line);
                start_hint_group(plugin_line, sizeof(plugin_line), "");
                width = 7;
            }
            append_key_hint(plugin_line, sizeof(plugin_line), p->keybind, p->name);
            width += entry_width;
        }
        if (width > 7) {
            render_line(state, plugin_line);
        }
    }
    plugin_list_free(&exp_plugins);
}
#endif

static bool files_are_different(const char *path1, const char *path2) {
    FILE *f1 = fopen(path1, "rb");
    FILE *f2 = fopen(path2, "rb");
    if (!f1 || !f2) {
        if (f1) fclose(f1);
        if (f2) fclose(f2);
        return true;
    }
    int c1, c2;
    do {
        c1 = fgetc(f1);
        c2 = fgetc(f2);
        if (c1 != c2) {
            fclose(f1);
            fclose(f2);
            return true;
        }
    } while (c1 != EOF && c2 != EOF);
    fclose(f1);
    fclose(f2);
    return false;
}

static void plugin_raw_url(const AppConfig *cfg, char *buf, size_t buf_size, const char *path) {
    snprintf(buf, buf_size, "https://raw.githubusercontent.com/%s/%s/addons/%s",
             cfg->plugin_repo, cfg->plugin_branch, path);
}

static void fetch_remote_plugins(AppState *state, const AppConfig *cfg, PluginList *list) {
    clear_owned_region(state);
    disable_raw_mode();
    enter_alt_screen();
    printf("Fetching remote plugin repository index...\n");
    fflush(stdout);

    char temp_index[PATH_MAX];
    snprintf(temp_index, sizeof(temp_index), "%s" PATH_SEP "temp_addons.txt", cfg->data_dir);

    char cmd[PATH_MAX * 2 + 512];
    char url[512];
    plugin_raw_url(cfg, url, sizeof(url), "addons.txt");
    snprintf(cmd, sizeof(cmd), "curl -s -f -L \"%s\" -o \"%s\"", url, temp_index);

    int ret = system(cmd);
    if (ret != 0) {
        printf("Error: failed to fetch remote plugins (network error or curl missing).\n");
        printf("Press any key to continue...");
        fflush(stdout);
        enable_raw_mode();
        read_key();
        exit_alt_screen();
        return;
    }

    FILE *f = fopen(temp_index, "r");
    if (!f) {
        exit_alt_screen();
        enable_raw_mode();
        return;
    }

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && isspace((unsigned char)line[len - 1])) {
            line[--len] = '\0';
        }
        if (len == 0 || !is_safe_url_part(line) || strchr(line, '/')) continue;

        Plugin *existing = NULL;
        for (size_t i = 0; i < list->count; i++) {
            if (strcmp(list->items[i].name, line) == 0) {
                existing = &list->items[i];
                break;
            }
        }

        if (existing) {
            existing->exists_on_remote = true;
            if (existing->is_compiled) {
                char temp_c[PATH_MAX];
                snprintf(temp_c, sizeof(temp_c), "%s" PATH_SEP "temp_update_%s.c", cfg->data_dir, line);
                char rel[PLUGIN_NAME_MAX * 2 + 8];
                snprintf(rel, sizeof(rel), "%s/%s.c", line, line);
                plugin_raw_url(cfg, url, sizeof(url), rel);
                snprintf(cmd, sizeof(cmd), "curl -s -f -L \"%s\" -o \"%s\"", url, temp_c);
                if (system(cmd) == 0) {
                    if (files_are_different(temp_c, existing->c_path)) {
                        existing->update_available = true;
                    }
                    unlink(temp_c);
                }
            }
            continue;
        }

        char temp_readme[PATH_MAX];
        snprintf(temp_readme, sizeof(temp_readme), "%s" PATH_SEP "temp_readme_%s.md", cfg->data_dir, line);

        char rel[PLUGIN_NAME_MAX + 16];
        snprintf(rel, sizeof(rel), "%s/README.md", line);
        plugin_raw_url(cfg, url, sizeof(url), rel);
        snprintf(cmd, sizeof(cmd), "curl -s -f -L \"%s\" -o \"%s\"", url, temp_readme);
        if (system(cmd) == 0) {
            Plugin p;
            memset(&p, 0, sizeof(p));
            snprintf(p.name, sizeof(p.name), "%s", line);
            snprintf(p.dir_path, sizeof(p.dir_path), "%s" PATH_SEP "%s", cfg->addons_dir, line);
            snprintf(p.c_path, sizeof(p.c_path), "%s" PATH_SEP "%s.c", p.dir_path, p.name);
#ifdef _WIN32
            snprintf(p.exe_path, sizeof(p.exe_path), "%s" PATH_SEP "%s.exe", p.dir_path, p.name);
#else
            snprintf(p.exe_path, sizeof(p.exe_path), "%s" PATH_SEP "%s", p.dir_path, p.name);
#endif
            p.is_remote = true;
            p.exists_on_remote = true;
            p.is_compiled = false;
            p.is_disabled = is_plugin_disabled_on_disk(cfg, p.name);

            if (parse_plugin_readme(temp_readme, &p)) {
                plugin_list_push(list, &p);
            }
            unlink(temp_readme);
        }
    }
    fclose(f);
    unlink(temp_index);

    exit_alt_screen();
    enable_raw_mode();
}

static bool copy_file(const char *src, const char *dst) {
    FILE *fsrc = fopen(src, "rb");
    if (!fsrc) return false;
    FILE *fdst = fopen(dst, "wb");
    if (!fdst) {
        fclose(fsrc);
        return false;
    }
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fsrc)) > 0) {
        fwrite(buf, 1, n, fdst);
    }
    fclose(fsrc);
    fclose(fdst);
    return true;
}

static bool confirm_plugin_action(AppState *state, const Plugin *plugin, const char *action) {
    if (plugin->api > 2) {
        snprintf(state->status, sizeof(state->status), "Plugin %s needs newer blob API.", plugin->name);
        return false;
    }

    char message[INPUT_MAX];
    snprintf(message, sizeof(message), "%s %s api=%d mode=%s perms=%s%s?",
             action,
             plugin->name,
             plugin->api,
             plugin->mode[0] ? plugin->mode : "note",
             plugin->permissions[0] ? plugin->permissions : "unknown",
             plugin->is_legacy ? " legacy" : "");
    return prompt_confirm(state, message);
}

static bool compile_plugin(AppState *state, const AppConfig *cfg, Plugin *plugin) {
    if (plugin->has_keybind_conflict) {
        snprintf(state->status, sizeof(state->status), "Plugin %s keybind conflicts.", plugin->name);
        return false;
    }
    if (plugin->api > 2) {
        snprintf(state->status, sizeof(state->status), "Plugin %s needs newer blob API.", plugin->name);
        return false;
    }
    if (cfg->plugin_confirm_install &&
        !confirm_plugin_action(state, plugin, plugin->is_compiled ? "Update" : "Install")) {
        return false;
    }

    clear_owned_region(state);
    disable_raw_mode();
    enter_alt_screen();

    // If it's a local plugin not in the data directory, "install" it by copying it there
    if (!plugin->is_remote && strstr(plugin->dir_path, cfg->addons_dir) == NULL) {
        printf("Installing local plugin to data directory...\n");
        char new_dir[PATH_MAX];
        snprintf(new_dir, sizeof(new_dir), "%s" PATH_SEP "%s", cfg->addons_dir, plugin->name);
        ensure_dir(new_dir);

        char new_c[PATH_MAX];
        snprintf(new_c, sizeof(new_c), "%s" PATH_SEP "%s.c", new_dir, plugin->name);
        if (!copy_file(plugin->c_path, new_c)) {
            printf("Error: failed to copy source file to data directory.\n");
            printf("Source: %s\n", plugin->c_path);
            printf("Dest: %s\n", new_c);
            printf("Press any key to continue...");
            fflush(stdout);
            enable_raw_mode();
            read_key();
            exit_alt_screen();
            return false;
        }

        char old_readme[PATH_MAX];
        snprintf(old_readme, sizeof(old_readme), "%s" PATH_SEP "README.md", plugin->dir_path);
        char new_readme[PATH_MAX];
        snprintf(new_readme, sizeof(new_readme), "%s" PATH_SEP "README.md", new_dir);
        copy_file(old_readme, new_readme);

        // Update paths only after successful copy
        snprintf(plugin->dir_path, sizeof(plugin->dir_path), "%s", new_dir);
        snprintf(plugin->c_path, sizeof(plugin->c_path), "%s", new_c);
#ifdef _WIN32
        snprintf(plugin->exe_path, sizeof(plugin->exe_path), "%s" PATH_SEP "%s.exe", plugin->dir_path, plugin->name);
#else
        snprintf(plugin->exe_path, sizeof(plugin->exe_path), "%s" PATH_SEP "%s", plugin->dir_path, plugin->name);
#endif
    }

    ensure_dir(plugin->dir_path);

    char cmd[PATH_MAX * 2 + 512];
    char url[512];
    char rel[PLUGIN_NAME_MAX * 2 + 16];

    if ((plugin->is_remote || plugin->update_available) && !is_safe_url_part(plugin->name)) {
        snprintf(state->status, sizeof(state->status), "Refusing to download plugin with unsafe name.");
        exit_alt_screen();
        enable_raw_mode();
        return false;
    }

    if (plugin->is_remote || plugin->update_available) {
        printf("Downloading plugin source files...\n");
        fflush(stdout);

        snprintf(rel, sizeof(rel), "%s/%s.c", plugin->name, plugin->name);
        plugin_raw_url(cfg, url, sizeof(url), rel);
        snprintf(cmd, sizeof(cmd), "curl -s -f -L \"%s\" -o \"%s\"", url, plugin->c_path);
        if (system(cmd) != 0) {
            printf("Error: failed to download C source file.\nPress any key to continue...");
            fflush(stdout);
            enable_raw_mode();
            read_key();
            exit_alt_screen();
            return false;
        }

        char readme_path[PATH_MAX];
        snprintf(readme_path, sizeof(readme_path), "%s" PATH_SEP "README.md", plugin->dir_path);
        snprintf(rel, sizeof(rel), "%s/README.md", plugin->name);
        plugin_raw_url(cfg, url, sizeof(url), rel);
        snprintf(cmd, sizeof(cmd), "curl -s -f -L \"%s\" -o \"%s\"", url, readme_path);
        system(cmd);
    }

    printf("Compiling plugin %s with optimizations...\n", plugin->name);
    fflush(stdout);

#ifdef _WIN32
    snprintf(cmd, sizeof(cmd), "gcc -Os -s \"%s\" -o \"%s\"", plugin->c_path, plugin->exe_path);
#else
    snprintf(cmd, sizeof(cmd), "cc -Os -s \"%s\" -o \"%s\"", plugin->c_path, plugin->exe_path);
#endif

    int ret = system(cmd);
    if (ret != 0) {
        printf("Compilation failed. Error code: %d\n", ret);
        printf("Make sure a C compiler (gcc/cc) is installed and in your PATH.\n");
        printf("Press any key to continue...");
        fflush(stdout);
        enable_raw_mode();
        read_key();
        exit_alt_screen();
        return false;
    }

    printf("Successfully compiled and installed: %s\n", plugin->name);
    printf("Press any key to continue...");
    fflush(stdout);
    enable_raw_mode();
    read_key();

    exit_alt_screen();

    plugin->is_compiled = true;
    plugin->is_remote = false;
    plugin->update_available = false;
    return true;
}

static void delete_plugin(AppState *state, const AppConfig *cfg, PluginList *list, size_t *selected, bool check_remote) {
    (void)check_remote;
    if (list->count == 0 || *selected >= list->count) return;
    Plugin *plugin = &list->items[*selected];

    char prompt_msg[128];
    snprintf(prompt_msg, sizeof(prompt_msg), "Uninstall plugin %s (delete files)?", plugin->name);
    if (!prompt_confirm(state, prompt_msg)) {
        return;
    }
    clear_owned_region(state);
    disable_raw_mode();
    enter_alt_screen();

    unlink(plugin->exe_path);
    // If it's in the data directory, clean up the source and readme as well
    if (strstr(plugin->dir_path, cfg->addons_dir) != NULL) {
        unlink(plugin->c_path);
        char readme_path[PATH_MAX];
        snprintf(readme_path, sizeof(readme_path), "%s" PATH_SEP "README.md", plugin->dir_path);
        unlink(readme_path);
    }

    printf("Plugin uninstalled.\nPress any key to continue...");
    fflush(stdout);
    enable_raw_mode();
    read_key();

    exit_alt_screen();

    plugin->is_compiled = false;
    plugin->update_available = false;
}

static bool run_plugin_process(const char *exe_path, const char *target_path) {
#ifdef _WIN32
    char command[PATH_MAX * 2 + 16];
    snprintf(command, sizeof(command), "\"%s\" \"%s\"", exe_path, target_path);

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);
bool ok = CreateProcessA(NULL, command, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi) != 0;
if (ok) {

        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    return ok;
#else
    pid_t pid = fork();
    if (pid == 0) {
        char *argv[] = {(char *)exe_path, (char *)target_path, NULL};
        execvp(exe_path, argv);
        _exit(127);
    } else if (pid > 0) {
        int status = 0;
        while (waitpid(pid, &status, 0) == -1 && errno == EINTR) {
        }
        return status == 0;
    }
    return false;
#endif
}

static void run_plugin_on_target(AppState *state, const Plugin *plugin, const char *target_path, bool ask) {
    if (plugin->has_keybind_conflict) {
        snprintf(state->status, sizeof(state->status), "Plugin %s keybind conflicts.", plugin->name);
        return;
    }
    if (plugin->api > 2) {
        snprintf(state->status, sizeof(state->status), "Plugin %s needs newer blob API.", plugin->name);
        return;
    }
    if (ask && g_plugin_confirm_run && !confirm_plugin_action(state, plugin, "Run")) {
        return;
    }

    clear_owned_region(state);
    disable_raw_mode();
    enter_alt_screen();

    printf("Executing plugin: %s...\n", plugin->name);
    fflush(stdout);

    if (!run_plugin_process(plugin->exe_path, target_path)) {
        printf("\nPlugin execution failed.\nPress any key to continue...");
        fflush(stdout);
        enable_raw_mode();
        read_key();
    }

    exit_alt_screen();
    enable_raw_mode();
    state->rendered_lines = 0;
}

static void run_plugin_on_note(AppState *state, const Plugin *plugin, const char *note_path) {
    run_plugin_on_target(state, plugin, note_path, true);
}

static void run_plugin_for_workspace(AppState *state, const Plugin *plugin, const AppConfig *cfg) {
    run_plugin_on_target(state, plugin, cfg->notes_dir, true);
    load_config((AppConfig *)cfg);
    load_theme(cfg);
}

static void describe_plugin_source(const AppConfig *cfg, const Plugin *p, char *buf, size_t buf_size) {
    if (p->is_remote) {
        snprintf(buf, buf_size, "github  %s@%s (not downloaded yet)", cfg->plugin_repo, cfg->plugin_branch);
        return;
    }

    char resolved[PATH_MAX];
    const char *shown = p->dir_path;
#ifndef _WIN32
    if (realpath(p->dir_path, resolved)) {
        shown = resolved;
    }
#else
    (void)resolved;
#endif

    char short_path[PATH_MAX];
    const char *home = getenv("HOME");
    size_t home_len = home ? strlen(home) : 0;
    if (home_len > 1 && strncmp(shown, home, home_len) == 0 && (shown[home_len] == '/' || shown[home_len] == '\0')) {
        snprintf(short_path, sizeof(short_path), "~%s", shown + home_len);
        shown = short_path;
    }

    bool installed = strstr(p->dir_path, cfg->addons_dir) != NULL;
    snprintf(buf, buf_size, "%s  %s%s",
             installed ? "installed" : "local",
             shown,
             p->exists_on_remote ? "  (also on github)" : "");
}

static void render_plugin_ui(AppState *state, const AppConfig *cfg, PluginList *plugins, size_t selected_plugin) {
    normalize_selection(state);
    clear_owned_region(state);

    char header[160];
    snprintf(header, sizeof(header), ANSI_BOLD "blob" ANSI_RESET "%s  \xc2\xb7  plugins  \xc2\xb7  %zu found" ANSI_RESET,
             g_theme.help, plugins->count);
    render_line(state, header);
    render_line(state, "");

    if (plugins->count == 0) {
        char line[64];
        snprintf(line, sizeof(line), "%s  no plugins found%s", g_theme.help, ANSI_RESET);
        render_line(state, line);
    } else {
        size_t max_name = 15;
        for (size_t i = 0; i < plugins->count; i++) {
            size_t len = strlen(plugins->items[i].name);
            if (len > max_name) max_name = len;
        }
        if (max_name > 25) max_name = 25;

        for (size_t i = 0; i < plugins->count; i++) {
            Plugin *p = &plugins->items[i];
            const char *status;
            if (p->is_disabled) {
                status = "disabled";
            } else if (p->has_keybind_conflict) {
                status = "key conflict";
            } else if (p->api > 2) {
                status = "needs newer blob";
            } else if (p->is_compiled) {
                status = p->update_available ? "update available" : "installed";
            } else if (p->is_remote) {
                status = "on github";
            } else {
                status = "not compiled";
            }

            char name[PLUGIN_NAME_MAX * 4 + 8];
            fit_column(name, sizeof(name), p->name, max_name);

            char key[8];
            snprintf(key, sizeof(key), "%c", p->keybind ? p->keybind : ' ');

            char line[512];
            if (i == selected_plugin) {
                snprintf(line, sizeof(line), "%s\xe2\x96\x8c" ANSI_RESET " " ANSI_BOLD "%s%s" ANSI_RESET "  %s%s  %s" ANSI_RESET,
                         g_theme.selected, g_theme.selected, name, g_theme.help, key, status);
            } else {
                snprintf(line, sizeof(line), "  %s%s" ANSI_RESET "  %s%s  %s" ANSI_RESET,
                         g_theme.title, name, g_theme.help, key, status);
            }
            render_line(state, line);
        }
    }

    render_line(state, "");

    if (plugins->count > 0 && selected_plugin < plugins->count) {
        Plugin *p = &plugins->items[selected_plugin];
        char source[PATH_MAX + 128];
        describe_plugin_source(cfg, p, source, sizeof(source));

        char version[64];
        snprintf(version, sizeof(version), "%s", p->version[0] ? p->version : "unknown");
        char keybind[8];
        snprintf(keybind, sizeof(keybind), "%c", p->keybind ? p->keybind : '-');
        char api_mode[64];
        snprintf(api_mode, sizeof(api_mode), "%d / %s%s", p->api, p->mode[0] ? p->mode : "note", p->is_legacy ? " (legacy)" : "");

        const char *labels[] = {"description", "author", "version", "keybind", "api / mode", "permissions", "source"};
        const char *values[] = {
            p->description[0] ? p->description : "none",
            p->authors[0] ? p->authors : "unknown",
            version, keybind, api_mode,
            p->permissions[0] ? p->permissions : "unknown",
            source,
        };

        int cols = terminal_columns();
        size_t width = cols > 16 ? (size_t)cols - 15 : 1;
        for (size_t i = 0; i < sizeof(labels) / sizeof(labels[0]); i++) {
            char value[PATH_MAX + 256];
            fit_column(value, sizeof(value), values[i], width);
            size_t end = strlen(value);
            while (end > 0 && value[end - 1] == ' ') value[--end] = '\0';

            char line[PATH_MAX + 320];
            snprintf(line, sizeof(line), "%s%-12s" ANSI_RESET " %s", g_theme.help, labels[i], value);
            render_line(state, line);
        }
        render_line(state, "");
    }

    char hints[1024];
    hints[0] = '\0';
    if (plugins->count > 0 && selected_plugin < plugins->count) {
        Plugin *p = &plugins->items[selected_plugin];
        if (p->api > 2) {
            append_hint(hints, sizeof(hints), "k", "key");
        } else if (p->has_keybind_conflict) {
            append_hint(hints, sizeof(hints), "enter", "pick a new key");
        } else if (!p->is_compiled) {
            append_hint(hints, sizeof(hints), "enter", p->is_remote ? "download & install" : "install");
            append_hint(hints, sizeof(hints), "k", "key");
        } else {
            if (p->update_available) {
                append_hint(hints, sizeof(hints), "enter", "update");
            }
            append_hint(hints, sizeof(hints), "k", "key");
            append_hint(hints, sizeof(hints), "u", "uninstall");
        }
        append_hint(hints, sizeof(hints), "t", p->is_disabled ? "enable" : "disable");
        render_line(state, hints);
    }
    hints[0] = '\0';
    append_hint(hints, sizeof(hints), "\xe2\x86\x91\xe2\x86\x93", "move");
    append_hint(hints, sizeof(hints), ",", "settings");
    append_hint(hints, sizeof(hints), "^P", "plugins off");
    append_hint(hints, sizeof(hints), "esc", "back");
    render_line(state, hints);

    if (state->status[0]) {
        render_line(state, "");
        char status_line[INPUT_MAX + 16];
        snprintf(status_line, sizeof(status_line), "%s%s" ANSI_RESET, g_theme.status, state->status);
        render_line(state, status_line);
        state->status[0] = '\0';
    }

    fflush(stdout);
}

#ifndef BLOB_TEST
static void settings_flow(AppState *state, AppConfig *cfg);

static void plugin_manager_flow(AppState *state, const AppConfig *cfg) {
    PluginList plugins = {NULL, 0, 0};

    scan_addons_dir(&plugins, cfg, cfg->addons_dir);
    if (cfg->plugin_scan_cwd) scan_addons_dir(&plugins, cfg, "addons");
    mark_plugin_keybind_conflicts(&plugins, cfg);

    if (!is_plugin_system_enabled(cfg)) {
        if (prompt_confirm(state, "Plugin system is disabled. Enable it?")) {
            set_plugin_system_enabled(cfg, true);
        } else {
            plugin_list_free(&plugins);
            return;
        }
    }

    bool check_remote = false;
    if (cfg->plugin_source == PLUGIN_SOURCE_GITHUB) {
        check_remote = true;
    } else if (cfg->plugin_source == PLUGIN_SOURCE_ASK) {
        char question[256];
        snprintf(question, sizeof(question), "Check github.com/%s for plugins?", cfg->plugin_repo);
        check_remote = prompt_confirm(state, question);
    }

    if (check_remote) {
        fetch_remote_plugins(state, cfg, &plugins);
        mark_plugin_keybind_conflicts(&plugins, cfg);
    }

    size_t selected = 0;
    bool in_menu = true;

    while (in_menu) {
        render_plugin_ui(state, cfg, &plugins, selected);
        KeyEvent key = read_key();

        if (key.type == KEY_UP) {
            if (selected > 0) selected--;
        } else if (key.type == KEY_DOWN) {
            if (plugins.count > 0 && selected + 1 < plugins.count) selected++;
        } else if (key.type == KEY_ESCAPE || (key.type == KEY_CHAR && key.ch == 'q')) {
            in_menu = false;
        } else if (key.type == KEY_CTRL_P) {
            if (prompt_confirm(state, "Disable plugin interface completely?")) {
                set_plugin_system_enabled(cfg, false);
                in_menu = false;
            }
        } else if (key.type == KEY_ENTER || (key.type == KEY_CHAR && key.ch == 'k')) {
            if (plugins.count > 0 && selected < plugins.count) {
                Plugin *p = &plugins.items[selected];
                if (p->has_keybind_conflict || key.ch == 'k') {
                    /* Keybind change flow */
                    clear_owned_region(state);
                    printf("\r" ANSI_CLEAR_LINE "Assign new keybind for \"%s\" (press a key, ESC to cancel): ", p->name);
                    fflush(stdout);
                    state->rendered_lines = 1;

                    KeyEvent new_key = read_key();
                    if (new_key.type == KEY_ESCAPE) {
                        clear_owned_region(state);
                        state->rendered_lines = 0;
                        snprintf(state->status, sizeof(state->status), "Keybind change cancelled.");
                    } else if (new_key.type == KEY_CHAR && new_key.ch) {
                        /* Check if the new key is taken */
                        char conflict_name[PLUGIN_NAME_MAX] = "";
                        for (size_t j = 0; j < plugins.count; j++) {
                            if (j != selected && plugins.items[j].keybind == new_key.ch) {
                                snprintf(conflict_name, sizeof(conflict_name), "%s", plugins.items[j].name);
                                break;
                            }
                        }
                        clear_owned_region(state);
                        state->rendered_lines = 0;
                        if (conflict_name[0]) {
                            snprintf(state->status, sizeof(state->status), "Key '%c' already used by \"%s\". Try 'k' to change again.", new_key.ch, conflict_name);
                        } else {
                            set_plugin_keybind_override(cfg, p->name, new_key.ch);
                            p->keybind = new_key.ch;
                            p->has_keybind_conflict = key_is_core_reserved(new_key.ch);
                            snprintf(state->status, sizeof(state->status), "\"%s\" keybind set to '%c'.", p->name, new_key.ch);
                        }
                    } else {
                        clear_owned_region(state);
                        state->rendered_lines = 0;
                        snprintf(state->status, sizeof(state->status), "Invalid key.");
                    }
                } else if (!p->is_compiled || p->update_available) {
                    compile_plugin(state, cfg, p);
                } else {
                    snprintf(state->status, sizeof(state->status), "Already compiled. Press '%c' on notes list.", p->keybind);
                }
            }
        } else if (key.type == KEY_CHAR && key.ch == 'u') {
            if (plugins.count > 0 && selected < plugins.count) {
                Plugin *p = &plugins.items[selected];
                if (p->is_compiled) {
                    delete_plugin(state, cfg, &plugins, &selected, check_remote);
                } else {
                    snprintf(state->status, sizeof(state->status), "%s is not installed.", p->name);
                }
            }
        } else if (key.type == KEY_CHAR && key.ch == ',') {
            settings_flow(state, (AppConfig *)cfg);
        } else if (key.type == KEY_CHAR && key.ch == 't') {
            if (plugins.count > 0 && selected < plugins.count) {
                Plugin *p = &plugins.items[selected];
                p->is_disabled = !p->is_disabled;
                set_plugin_disabled_on_disk(cfg, p->name, p->is_disabled);
                snprintf(state->status, sizeof(state->status), "%s %s.", p->name, p->is_disabled ? "disabled" : "enabled");
            }
        }
    }

    plugin_list_free(&plugins);
    clear_owned_region(state);
}
#endif

static bool is_note_encrypted(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    char buf[64];
    size_t r = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    if (r < 21) return false;
    buf[r] = '\0';
    return (strncmp(buf, "--- BLOB CRYPT V1 ---", 21) == 0) ||
           (strncmp(buf, "--- BLOB CRYPT V2 ---", 21) == 0);
}

static void open_selected_note(AppState *state, const AppConfig *cfg) {
    if (state->notes.count == 0 || !selected_is_visible(state)) {
        return;
    }

    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s", state->notes.items[state->selected].path);

    bool encrypted = is_note_encrypted(path);
    bool unlocked = false;

    if (encrypted) {
        PluginList temp_plugins = {NULL, 0, 0};
        scan_addons_dir(&temp_plugins, cfg, cfg->addons_dir);
        if (cfg->plugin_scan_cwd) scan_addons_dir(&temp_plugins, cfg, "addons");

        Plugin *lock_plugin = NULL;
        for (size_t i = 0; i < temp_plugins.count; i++) {
            if (strcmp(temp_plugins.items[i].name, "lock") == 0 && temp_plugins.items[i].is_compiled) {
                lock_plugin = &temp_plugins.items[i];
                break;
            }
        }

        if (lock_plugin) {
            run_plugin_on_note(state, lock_plugin, path);
            if (!is_note_encrypted(path)) {
                unlocked = true;
            } else {
                plugin_list_free(&temp_plugins);
                return;
            }
        } else {
            snprintf(state->status, sizeof(state->status), "Note is locked, but 'lock' plugin is not installed.");
            plugin_list_free(&temp_plugins);
            return;
        }

        plugin_list_free(&temp_plugins);
    }

    open_path_in_editor(state, cfg, path);

    if (unlocked) {
        PluginList temp_plugins = {NULL, 0, 0};
        scan_addons_dir(&temp_plugins, cfg, cfg->addons_dir);
        if (cfg->plugin_scan_cwd) scan_addons_dir(&temp_plugins, cfg, "addons");

        Plugin *lock_plugin = NULL;
        for (size_t i = 0; i < temp_plugins.count; i++) {
            if (strcmp(temp_plugins.items[i].name, "lock") == 0 && temp_plugins.items[i].is_compiled) {
                lock_plugin = &temp_plugins.items[i];
                break;
            }
        }

        if (lock_plugin) {
            clear_owned_region(state);
            run_plugin_on_note(state, lock_plugin, path);
        }
        plugin_list_free(&temp_plugins);
    }

    clear_owned_region(state);

    load_notes(&state->notes, cfg);
    load_favorites_for_list(&state->notes, cfg);
    normalize_selection(state);
}

static void lower_ascii(char *s) {
    for (; *s; s++) {
        *s = (char)tolower((unsigned char)*s);
    }
}

#ifndef BLOB_TEST
static void quick_view_flow(AppState *state, const AppConfig *cfg) {
    if (state->notes.count == 0 || !selected_is_visible(state)) {
        return;
    }

    const Note *selected = &state->notes.items[state->selected];
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s", selected->path);

    FILE *f = fopen(path, "r");
    if (!f) {
        snprintf(state->status, sizeof(state->status), "cannot read note: %s", strerror(errno));
        return;
    }

    // Read all lines into memory
    char **lines = NULL;
    size_t line_count = 0;
    size_t capacity = 0;
    char buf[1024];
    while (fgets(buf, sizeof(buf), f)) {
        size_t len = strlen(buf);
        while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
            buf[--len] = '\0';
        }
        if (line_count == capacity) {
            capacity = capacity ? capacity * 2 : 64;
            char **next = realloc(lines, capacity * sizeof(*next));
            if (!next) break;
            lines = next;
        }
        lines[line_count] = malloc(len + 1);
        if (!lines[line_count]) break;
        memcpy(lines[line_count], buf, len + 1);
        line_count++;
    }
    fclose(f);

    size_t scroll = 0;
    bool running = true;

    while (running) {
        clear_owned_region(state);

        char header[TITLE_MAX + 32];
        snprintf(header, sizeof(header), "%sblob: view — %s%s", ANSI_BOLD, selected->title, ANSI_RESET);
        render_line(state, header);
        render_line(state, "");

        if (line_count == 0) {
            render_line(state, ANSI_DIM "(empty note)" ANSI_RESET);
        } else {
            int cols = terminal_columns();
            size_t width = cols > 1 ? (size_t)cols - 1 : 1;
            size_t shown = 0;
            for (size_t i = scroll; i < line_count && shown < QUICK_VIEW_LINES; i++, shown++) {
                char expanded[4200];
                size_t e = 0;
                for (const char *c = lines[i]; *c && e + 5 < sizeof(expanded); c++) {
                    if (*c == '\t') {
                        memcpy(expanded + e, "    ", 4);
                        e += 4;
                    } else {
                        expanded[e++] = *c;
                    }
                }
                expanded[e] = '\0';

                char fitted[4300];
                fit_column(fitted, sizeof(fitted), expanded, width);
                size_t end = strlen(fitted);
                while (end > 0 && fitted[end - 1] == ' ') fitted[--end] = '\0';

                char line[4400];
                snprintf(line, sizeof(line), "%s%s" ANSI_RESET, shown == 0 ? g_theme.selected : "", fitted);
                render_line(state, line);
            }
            if (line_count > QUICK_VIEW_LINES) {
                char page_info[64];
                size_t end_visible = scroll + shown > line_count ? line_count : scroll + shown;
                snprintf(page_info, sizeof(page_info), "%sLines %zu-%zu of %zu%s", g_theme.pagination,
                         scroll + 1, end_visible, line_count, ANSI_RESET);
                render_line(state, page_info);
            }
        }

        render_line(state, "");
        render_line(state, ANSI_DIM "────────────────────────────────" ANSI_RESET);
        render_line(state, "");

        char help_line[128];
        snprintf(help_line, sizeof(help_line), "%s[%c/%c] scroll  [g/G] top/bottom  [e] edit%s", g_theme.help,
                 cfg->key_move_up, cfg->key_move_down, ANSI_RESET);
        render_line(state, help_line);
        snprintf(help_line, sizeof(help_line), "%s[ESC/%c] back%s", g_theme.help, cfg->key_quit, ANSI_RESET);
        render_line(state, help_line);
        fflush(stdout);

        KeyEvent key = read_key();
        if (key.type == KEY_UP || (key.type == KEY_CHAR && key.ch == cfg->key_move_up)) {
            if (scroll > 0) scroll--;
        } else if (key.type == KEY_DOWN || (key.type == KEY_CHAR && key.ch == cfg->key_move_down)) {
            if (line_count > 0 && scroll + QUICK_VIEW_LINES < line_count) scroll++;
        } else if (key.type == KEY_PGUP) {
            scroll = scroll > QUICK_VIEW_LINES ? scroll - QUICK_VIEW_LINES : 0;
        } else if (key.type == KEY_PGDN) {
            scroll += QUICK_VIEW_LINES;
            if (scroll >= line_count && line_count > 0) {
                scroll = line_count - 1;
            }
        } else if (key.type == KEY_HOME || (key.type == KEY_CHAR && key.ch == 'g')) {
            scroll = 0;
        } else if (key.type == KEY_END || (key.type == KEY_CHAR && key.ch == 'G')) {
            if (line_count > 0) scroll = line_count > QUICK_VIEW_LINES ? line_count - QUICK_VIEW_LINES : 0;
        } else if (key.type == KEY_CHAR && key.ch == 'e') {
            running = false;
            open_path_in_editor(state, cfg, path);
        } else if (key.type == KEY_ESCAPE || (key.type == KEY_CHAR && key.ch == cfg->key_quit)) {
            running = false;
        }
    }

    for (size_t i = 0; i < line_count; i++) {
        free(lines[i]);
    }
    free(lines);
    clear_owned_region(state);
}
#endif

#ifndef BLOB_TEST
static int build_doctor_report(const AppConfig *cfg, char *buf, size_t buf_size) {
    int failed = 0;
    size_t off = 0;

#define REPORT(...) \
    do { \
        int n = snprintf(buf + off, buf_size - off, __VA_ARGS__); \
        if (n > 0) off += (size_t)n; \
        if (off >= buf_size) return failed; \
    } while (0)

    REPORT("blob doctor\n");
    REPORT("===========\n");

    struct stat st;
    if (stat(cfg->data_dir, &st) == 0 && STAT_ISDIR(st.st_mode)) {
        REPORT("[ok]  data dir exists: %s\n", cfg->data_dir);
    } else {
        REPORT("[!!]  data dir missing: %s\n", cfg->data_dir);
        failed = 1;
    }
    if (stat(cfg->notes_dir, &st) == 0 && STAT_ISDIR(st.st_mode)) {
        REPORT("[ok]  notes dir exists: %s\n", cfg->notes_dir);
    } else {
        REPORT("[!!]  notes dir missing: %s\n", cfg->notes_dir);
        failed = 1;
    }
    FILE *f = fopen(cfg->config_path, "r");
    if (f) {
        fclose(f);
        REPORT("[ok]  config file readable\n");
    } else {
        REPORT("[..]  no config file (defaults in use)\n");
    }
    f = fopen(cfg->favorites_path, "r");
    if (f) {
        fclose(f);
        REPORT("[ok]  favorites file readable\n");
    } else {
        REPORT("[..]  no favorites file\n");
    }
    char kb_path[PATH_MAX + 64];
    snprintf(kb_path, sizeof(kb_path), "%s" PATH_SEP "plugin_keybinds", cfg->data_dir);
    f = fopen(kb_path, "r");
    if (f) {
        fclose(f);
        REPORT("[ok]  plugin keybinds file readable\n");
    } else {
        REPORT("[..]  no plugin keybind overrides\n");
    }
    PluginList exp_plugins = {NULL, 0, 0};
    scan_addons_dir(&exp_plugins, cfg, cfg->addons_dir);
    if (cfg->plugin_scan_cwd) scan_addons_dir(&exp_plugins, cfg, "addons");
    mark_plugin_keybind_conflicts(&exp_plugins, cfg);
    for (size_t i = 0; i < exp_plugins.count; i++) {
        Plugin *p = &exp_plugins.items[i];
        if (p->is_compiled) {
            REPORT("[ok]  plugin \"%s\" compiled\n", p->name);
        } else {
            REPORT("[!!]  plugin \"%s\" not compiled\n", p->name);
            failed = 1;
        }
    }
    plugin_list_free(&exp_plugins);
    if (is_plugin_system_enabled(cfg)) {
        REPORT("[ok]  plugin system enabled\n");
    } else {
        REPORT("[..]  plugin system disabled\n");
    }
    char session_path[PATH_MAX];
    snprintf(session_path, sizeof(session_path), "%s" PATH_SEP "session", cfg->data_dir);
    f = fopen(session_path, "r");
    if (f) {
        fclose(f);
        REPORT("[ok]  session file present\n");
    } else {
        REPORT("[..]  no session file yet\n");
    }

    if (failed) {
        REPORT("\nSome checks failed. Fix the [!!] items above.\n");
    } else {
        REPORT("\nAll checks passed.\n");
    }
    return failed;
#undef REPORT
}
#endif

#ifndef BLOB_TEST
static void doctor_flow(AppState *state, const AppConfig *cfg) {
    char report[4096];
    build_doctor_report(cfg, report, sizeof(report));

    clear_owned_region(state);
    disable_raw_mode();
    printf("%s\n", report);
    printf("Press ESC/q to return...");
    fflush(stdout);
    enable_raw_mode();

    KeyEvent k;
    do { k = read_key(); } while (k.type == KEY_NONE);
    printf("\n");

    int cols = terminal_columns();
    size_t width = cols > 0 ? (size_t)cols : 80;
    size_t printed = 2;
    size_t line_cols = 0;
    for (const char *c = report; *c; c++) {
        if (*c == '\n') {
            printed += line_cols > 0 ? (line_cols - 1) / width : 0;
            printed++;
            line_cols = 0;
        } else if ((((unsigned char)*c) & 0xC0) != 0x80) {
            line_cols++;
        }
    }
    printed += line_cols > 0 ? (line_cols - 1) / width : 0;
    state->rendered_lines = printed;
    clear_owned_region(state);
}
#endif

static bool run_named_plugin(AppState *state, const AppConfig *cfg, const char *name) {
    PluginList plugins = {NULL, 0, 0};
    scan_addons_dir(&plugins, cfg, cfg->addons_dir);
    if (cfg->plugin_scan_cwd) scan_addons_dir(&plugins, cfg, "addons");
    mark_plugin_keybind_conflicts(&plugins, cfg);

    for (size_t i = 0; i < plugins.count; i++) {
        Plugin *p = &plugins.items[i];
        if (contains_case_insensitive(p->name, name) && p->is_compiled && !p->is_disabled) {
            if (plugin_uses_workspace(p)) {
                run_plugin_for_workspace(state, p, cfg);
            } else if (state->notes.count > 0 && selected_is_visible(state)) {
                run_plugin_on_note(state, p, state->notes.items[state->selected].path);
            } else {
                snprintf(state->status, sizeof(state->status), "no selected note");
            }
            plugin_list_free(&plugins);
            load_notes(&state->notes, cfg);
            load_favorites_for_list(&state->notes, cfg);
            normalize_selection(state);
            return true;
        }
    }

    plugin_list_free(&plugins);
    return false;
}

#ifndef BLOB_TEST
static void show_reminders_flow(AppState *state, const AppConfig *cfg) {
    /* reminders.txt format: one reminder per line
     * <ring_at_unix_timestamp>\t<note_title>\n */
    char rem_path[PATH_MAX];
    snprintf(rem_path, sizeof(rem_path), "%s" PATH_SEP "reminders.txt", cfg->data_dir);

    clear_owned_region(state);
    render_line(state, ANSI_BOLD "blob: active reminders" ANSI_RESET);
    render_line(state, "");

    FILE *f = fopen(rem_path, "r");
    if (!f) {
        render_line(state, ANSI_DIM "no reminders set" ANSI_RESET);
        render_line(state, "");
        render_line(state, ANSI_DIM "────────────────────────────────" ANSI_RESET);
        render_line(state, "");
        char hl[128];
        snprintf(hl, sizeof(hl), "%s[ESC/q] back%s", g_theme.help, ANSI_RESET);
        render_line(state, hl);
        fflush(stdout);
        KeyEvent k = read_key();
        (void)k;
        clear_owned_region(state);
        return;
    }

    time_t now = time(NULL);

    /* First pass: read all, decide which to keep, collect display rows */
    typedef struct { long ring_at; char title[256]; } Rem;
    Rem rems[64];
    size_t nrems = 0;

    char line[512];
    while (fgets(line, sizeof(line), f) && nrems < 64) {
        long ring_at = 0;
        char title[256];
        title[0] = '\0';
        /* parse: "<timestamp>\t<title>\n" */
        char *tab = strchr(line, '\t');
        if (!tab) continue;
        *tab = '\0';
        ring_at = atol(line);
        snprintf(title, sizeof(title), "%s", tab + 1);
        /* strip trailing newline */
        size_t tlen = strlen(title);
        while (tlen > 0 && (title[tlen-1] == '\n' || title[tlen-1] == '\r')) {
            title[--tlen] = '\0';
        }
        if (ring_at <= 0 || title[0] == '\0') continue;
        /* drop reminders that fired more than 1 hour ago */
        if ((long)now - ring_at > 3600) continue;
        rems[nrems].ring_at = ring_at;
        snprintf(rems[nrems].title, sizeof(rems[nrems].title), "%s", title);
        nrems++;
    }
    fclose(f);

    /* rewrite file with only kept entries */
    f = fopen(rem_path, "w");
    if (f) {
        for (size_t i = 0; i < nrems; i++) {
            fprintf(f, "%ld\t%s\n", rems[i].ring_at, rems[i].title);
        }
        fclose(f);
    }

    if (nrems == 0) {
        render_line(state, ANSI_DIM "no active reminders" ANSI_RESET);
    } else {
        for (size_t i = 0; i < nrems; i++) {
            long diff = rems[i].ring_at - (long)now;
            char time_str[64];
            if (diff > 0) {
                long h = diff / 3600;
                long m = (diff % 3600) / 60;
                long s = diff % 60;
                if (h > 0)      snprintf(time_str, sizeof(time_str), "in %ldh %ldm", h, m);
                else if (m > 0) snprintf(time_str, sizeof(time_str), "in %ldm %lds", m, s);
                else            snprintf(time_str, sizeof(time_str), "in %lds", s);
            } else {
                long ago = -diff;
                long m = ago / 60;
                snprintf(time_str, sizeof(time_str), "fired %ldm ago", m);
            }
            char row[512];
            snprintf(row, sizeof(row), "  %s%-40s%s %s%s%s",
                     g_theme.title, rems[i].title, ANSI_RESET,
                     g_theme.timestamp, time_str, ANSI_RESET);
            render_line(state, row);
        }
    }

    render_line(state, "");
    render_line(state, ANSI_DIM "────────────────────────────────" ANSI_RESET);
    render_line(state, "");
    char hl[128];
    snprintf(hl, sizeof(hl), "%s[ESC/q] back%s", g_theme.help, ANSI_RESET);
    render_line(state, hl);
    fflush(stdout);

    /* wait for any keypress to dismiss */
    KeyEvent k;
    do { k = read_key(); } while (k.type == KEY_NONE);
    clear_owned_region(state);
}

/* ── keybinding config UI ───────────────────────────────────── */
#endif

typedef struct {
    const char *name;
    const char *label;
    char *field; // pointer to the char field in AppConfig
} KeybindingEntry;

#ifndef BLOB_TEST
static void keybindings_config_flow(AppState *state, AppConfig *cfg) {
    KeybindingEntry bindings[] = {
        {"create",     "New note",     &cfg->key_create},
        {"rename",     "Rename",      &cfg->key_rename},
        {"trash",      "Trash",       &cfg->key_trash},
        {"trash_bin",  "Trash bin",   &cfg->key_trash_bin},
        {"copy",       "Copy path",   &cfg->key_copy},
        {"star",       "Star",        &cfg->key_star},
        {"search",     "Search",      &cfg->key_search},
        {"cmd",        "Command",     &cfg->key_cmd},
        {"plugins",    "Plugins",     &cfg->key_plugins},
        {"quit",       "Quit",        &cfg->key_quit},
        {"undo",       "Undo",        &cfg->key_undo},
        {"move_down",  "Move down",   &cfg->key_move_down},
        {"move_up",    "Move up",     &cfg->key_move_up},
        {"view",       "Quick view",  &cfg->key_view},
    };
    size_t num_bindings = sizeof(bindings) / sizeof(bindings[0]);
    size_t sel = 0;
    bool running = true;
    bool modified = false;
    char status_msg[INPUT_MAX] = "";

    while (running) {
        clear_owned_region(state);
        render_line(state, ANSI_BOLD "blob: configure keybindings" ANSI_RESET);
        render_line(state, "");

        /* Show 12 visible entries with scrolling */
        size_t start = 0;
        if (sel >= VISIBLE_NOTES) start = sel - VISIBLE_NOTES + 1;
        size_t shown = 0;
        for (size_t i = start; i < num_bindings && shown < VISIBLE_NOTES; i++, shown++) {
            char line[128];
            char key_str[8] = {bindings[i].field[0], '\0', '\0', '\0', '\0', '\0', '\0', '\0'};
            if (bindings[i].field[0] == '\0') {
                snprintf(key_str, sizeof(key_str), "?");
            } else if (bindings[i].field[0] == ' ') {
                snprintf(key_str, sizeof(key_str), "Space");
            } else if (bindings[i].field[0] == 127) {
                snprintf(key_str, sizeof(key_str), "Bksp");
            } else {
                key_str[0] = bindings[i].field[0];
                key_str[1] = '\0';
            }
            const char *prefix = (i == sel) ? g_theme.selected : "";
            snprintf(line, sizeof(line), "%s> %-14s %s[%s]%s",
                     prefix,
                     bindings[i].label,
                     g_theme.title,
                     key_str,
                     ANSI_RESET);
            render_line(state, line);
        }

        render_line(state, "");
        render_line(state, ANSI_DIM "────────────────────────────────" ANSI_RESET);
        render_line(state, "");

        char help[128];
        snprintf(help, sizeof(help), "%s[%c/%c] navigate  [ENTER] edit%s", g_theme.help, cfg->key_move_up, cfg->key_move_down, ANSI_RESET);
        render_line(state, help);
        snprintf(help, sizeof(help), "%s[Ctrl+S] save  [Ctrl+D] discard%s", g_theme.help, ANSI_RESET);
        render_line(state, help);
        snprintf(help, sizeof(help), "%s[ESC/q] back%s", g_theme.help, ANSI_RESET);
        render_line(state, help);

        if (status_msg[0]) {
            render_line(state, "");
            char sl[INPUT_MAX + 16];
            snprintf(sl, sizeof(sl), "%s%s%s", g_theme.status, status_msg, ANSI_RESET);
            render_line(state, sl);
            status_msg[0] = '\0';
        }

        fflush(stdout);

        KeyEvent key = read_key();

        if (key.type == KEY_UP || (key.type == KEY_CHAR && key.ch == cfg->key_move_up)) {
            if (sel > 0) sel--;
        } else if (key.type == KEY_DOWN || (key.type == KEY_CHAR && key.ch == cfg->key_move_down)) {
            if (sel + 1 < num_bindings) sel++;
        } else if (key.type == KEY_ESCAPE ||
                   (key.type == KEY_CHAR && (key.ch == 'q' || key.ch == cfg->key_quit))) {
            if (modified) {
                snprintf(status_msg, sizeof(status_msg), "Changes discarded!");
                /* Reload original config to discard changes */
                load_config(cfg);
                load_theme(cfg);
            }
            running = false;
        } else if (key.type == KEY_CTRL_S) {
            save_config(cfg);
            load_theme(cfg);
            snprintf(status_msg, sizeof(status_msg), "Keybindings saved!");
            modified = false;
        } else if (key.type == KEY_CTRL_D) {
            if (modified) {
                load_config(cfg);
                load_theme(cfg);
            }
            snprintf(status_msg, sizeof(status_msg), "Changes discarded!");
            modified = false;
            running = false;
        } else if (key.type == KEY_ENTER) {
            /* Edit the selected keybinding */
            clear_owned_region(state);
            disable_raw_mode();
            printf("  Press new key for \"%s\" (currently [%c]):\n",
                   bindings[sel].label,
                   *bindings[sel].field ? *bindings[sel].field : '?');
            printf("  > ");
            fflush(stdout);
            enable_raw_mode();

            KeyEvent new_key = read_key();
            if (new_key.type == KEY_CHAR && new_key.ch != 27) {
                *bindings[sel].field = new_key.ch;
                modified = true;
                snprintf(status_msg, sizeof(status_msg), "Set \"%s\" to [%c]", bindings[sel].label, new_key.ch);
            } else if (new_key.type == KEY_ESCAPE) {
                snprintf(status_msg, sizeof(status_msg), "Cancelled");
            }
        }
    }

    clear_owned_region(state);
}
#endif

#ifndef BLOB_TEST
/* ── settings panel ────────────────────────────────────────────────────── */

typedef enum {
    SET_EDITOR,
    SET_THEME,
    SET_SORT,
    SET_SORT_REVERSE,
    SET_RESTORE_SESSION,
    SET_VISIBLE_NOTES,
    SET_DATE_STYLE,
    SET_COLOR_MODE,
    SET_SHOW_HINTS,
    SET_CONFIRM_TRASH,
    SET_OPEN_AFTER_CREATE,
    SET_PURGE_DAYS,
    SET_PLUGINS_ENABLED,
    SET_PLUGIN_SOURCE,
    SET_PLUGIN_CONFIRM_RUN,
    SET_PLUGIN_CONFIRM_INSTALL,
    SET_PLUGIN_SCAN_CWD,
    SET_PLUGIN_REPO,
    SET_PLUGIN_BRANCH,
    SET_EDIT_KEYS,
    SET_RESET_KEYS
} SettingId;

typedef struct {
    SettingId id;
    int tab;
    const char *label;
    const char *help;
} SettingDef;

static const char *s_setting_tabs[] = {"general", "display", "notes", "plugins", "keys"};
#define SETTING_TAB_COUNT ((int)(sizeof(s_setting_tabs) / sizeof(s_setting_tabs[0])))

static const SettingDef s_settings[] = {
    {SET_EDITOR, 0, "editor", "Command used to open notes. Overrides $EDITOR. Arguments are allowed, e.g. \"code -w\"."},
    {SET_THEME, 0, "theme", NULL},
    {SET_SORT, 0, "sort by", "Order of the note list. Starred notes always stay on top."},
    {SET_SORT_REVERSE, 0, "reverse order", "Flip the sort direction."},
    {SET_RESTORE_SESSION, 0, "reopen last note", "Select the note you had open last time when blob starts."},
    {SET_VISIBLE_NOTES, 1, "notes per page", "How many notes the list shows before it scrolls."},
    {SET_DATE_STYLE, 1, "dates", "relative shows \"3h ago\", absolute shows \"Oct 03 14:20\"."},
    {SET_COLOR_MODE, 1, "colours", "auto picks 24-bit colour when the terminal supports it. Use 256 if themes look wrong."},
    {SET_SHOW_HINTS, 1, "hint bar", "The shortcut line under the list. ? still shows all keys when it is hidden."},
    {SET_CONFIRM_TRASH, 2, "ask before trashing", "Ask \"Move to trash?\" when you press d. Trash can always be undone with u."},
    {SET_OPEN_AFTER_CREATE, 2, "open new notes", "Open the editor straight after creating a note."},
    {SET_PURGE_DAYS, 2, "empty trash after", "Trashed notes older than this are deleted when blob starts."},
    {SET_PLUGINS_ENABLED, 3, "plugins", "Turn the whole plugin system on or off."},
    {SET_PLUGIN_SOURCE, 3, "find plugins on", "local: installed and ./addons only. ask: ask before contacting GitHub. github: always check GitHub."},
    {SET_PLUGIN_CONFIRM_RUN, 3, "ask before running", "Show a plugin's permissions and ask before it runs. Turning this off runs plugins straight away."},
    {SET_PLUGIN_CONFIRM_INSTALL, 3, "ask before installing", "Show a plugin's permissions and ask before it is compiled or updated."},
    {SET_PLUGIN_SCAN_CWD, 3, "include ./addons", "Also list plugins from an addons folder in the directory blob was started in."},
    {SET_PLUGIN_REPO, 3, "github repo", "owner/name of the repository plugins are downloaded from."},
    {SET_PLUGIN_BRANCH, 3, "github branch", "Branch plugins are downloaded from."},
    {SET_EDIT_KEYS, 4, "edit keybindings", "Open the keybinding editor (same as Ctrl+K)."},
    {SET_RESET_KEYS, 4, "reset keybindings", "Put every core key back to its default."},
};
#define SETTING_COUNT (sizeof(s_settings) / sizeof(s_settings[0]))

static const int s_visible_note_steps[] = {5, 8, 10, 12, 15, 20, 25, 30};
static const int s_purge_day_steps[] = {0, 7, 14, 30, 60, 90, 180};

static int step_cycle(const int *steps, size_t count, int current, int dir) {
    size_t at = 0;
    for (size_t i = 0; i < count; i++) {
        if (steps[i] <= current) at = i;
    }
    if (dir > 0) at = at + 1 < count ? at + 1 : 0;
    else at = at > 0 ? at - 1 : count - 1;
    return steps[at];
}

static void setting_value(const AppConfig *cfg, SettingId id, char *buf, size_t buf_size) {
    switch (id) {
    case SET_EDITOR: snprintf(buf, buf_size, "%s", cfg->editor); break;
    case SET_THEME: snprintf(buf, buf_size, "%s", cfg->theme_name); break;
    case SET_SORT:
        snprintf(buf, buf_size, "%s", g_sort_mode == SORT_TITLE ? "title" : g_sort_mode == SORT_SIZE ? "size" : "last edited");
        break;
    case SET_SORT_REVERSE: snprintf(buf, buf_size, "%s", cfg->sort_reverse ? "on" : "off"); break;
    case SET_RESTORE_SESSION: snprintf(buf, buf_size, "%s", cfg->restore_session ? "on" : "off"); break;
    case SET_VISIBLE_NOTES: snprintf(buf, buf_size, "%d", cfg->visible_notes); break;
    case SET_DATE_STYLE: snprintf(buf, buf_size, "%s", cfg->date_absolute ? "absolute" : "relative"); break;
    case SET_COLOR_MODE:
        if (cfg->color_mode == COLOR_MODE_TRUE) snprintf(buf, buf_size, "24-bit");
        else if (cfg->color_mode == COLOR_MODE_256) snprintf(buf, buf_size, "256");
        else snprintf(buf, buf_size, "auto (%s)", terminal_supports_true_color() ? "24-bit" : "256");
        break;
    case SET_SHOW_HINTS: snprintf(buf, buf_size, "%s", cfg->show_hints ? "shown" : "hidden"); break;
    case SET_CONFIRM_TRASH: snprintf(buf, buf_size, "%s", cfg->confirm_trash ? "on" : "off"); break;
    case SET_OPEN_AFTER_CREATE: snprintf(buf, buf_size, "%s", cfg->open_after_create ? "on" : "off"); break;
    case SET_PURGE_DAYS:
        if (cfg->purge_days <= 0) snprintf(buf, buf_size, "never");
        else snprintf(buf, buf_size, "%d days", cfg->purge_days);
        break;
    case SET_PLUGINS_ENABLED: snprintf(buf, buf_size, "%s", is_plugin_system_enabled(cfg) ? "on" : "off"); break;
    case SET_PLUGIN_SOURCE:
        snprintf(buf, buf_size, "%s", cfg->plugin_source == PLUGIN_SOURCE_LOCAL ? "local only" :
                                      cfg->plugin_source == PLUGIN_SOURCE_GITHUB ? "github" : "ask before github");
        break;
    case SET_PLUGIN_CONFIRM_RUN: snprintf(buf, buf_size, "%s", cfg->plugin_confirm_run ? "on" : "off"); break;
    case SET_PLUGIN_CONFIRM_INSTALL: snprintf(buf, buf_size, "%s", cfg->plugin_confirm_install ? "on" : "off"); break;
    case SET_PLUGIN_SCAN_CWD: snprintf(buf, buf_size, "%s", cfg->plugin_scan_cwd ? "on" : "off"); break;
    case SET_PLUGIN_REPO: snprintf(buf, buf_size, "%s", cfg->plugin_repo); break;
    case SET_PLUGIN_BRANCH: snprintf(buf, buf_size, "%s", cfg->plugin_branch); break;
    case SET_EDIT_KEYS:
    case SET_RESET_KEYS:
        snprintf(buf, buf_size, "%s", "enter");
        break;
    }
}

/* Applies one change to a setting. dir is +1/-1 for cycling, 0 for enter */
static void setting_change(AppState *state, AppConfig *cfg, SettingId id, int dir) {
    int step = dir == 0 ? 1 : dir;

    switch (id) {
    case SET_EDITOR: {
        char value[INPUT_MAX];
        if (prompt_text(state, "Editor command", value, sizeof(value))) {
            snprintf(cfg->editor, sizeof(cfg->editor), "%s", value);
        }
        break;
    }
    case SET_THEME: {
        size_t at = 0;
        const char *current = resolve_theme_name(cfg->theme_name);
        for (size_t i = 0; i < s_theme_count; i++) {
            if (strcmp(s_themes[i].name, current) == 0) at = i;
        }
        at = (at + s_theme_count + (size_t)(step > 0 ? 1 : s_theme_count - 1)) % s_theme_count;
        snprintf(cfg->theme_name, sizeof(cfg->theme_name), "%s", s_themes[at].name);
        load_theme(cfg);
        break;
    }
    case SET_SORT: {
        int mode = ((int)g_sort_mode + (step > 0 ? 1 : 2)) % 3;
        g_sort_mode = (SortMode)mode;
        snprintf(cfg->sort_order, sizeof(cfg->sort_order), "%s",
                 g_sort_mode == SORT_TITLE ? "title" : g_sort_mode == SORT_SIZE ? "size" : "mtime");
        break;
    }
    case SET_SORT_REVERSE:
        cfg->sort_reverse = !cfg->sort_reverse;
        g_sort_reverse = cfg->sort_reverse;
        break;
    case SET_RESTORE_SESSION: cfg->restore_session = !cfg->restore_session; break;
    case SET_VISIBLE_NOTES:
        cfg->visible_notes = step_cycle(s_visible_note_steps, sizeof(s_visible_note_steps) / sizeof(int), cfg->visible_notes, step);
        g_visible_notes = cfg->visible_notes;
        break;
    case SET_DATE_STYLE:
        cfg->date_absolute = !cfg->date_absolute;
        g_date_absolute = cfg->date_absolute;
        break;
    case SET_COLOR_MODE:
        cfg->color_mode = (ColorMode)(((int)cfg->color_mode + (step > 0 ? 1 : 2)) % 3);
        load_theme(cfg);
        break;
    case SET_SHOW_HINTS: cfg->show_hints = !cfg->show_hints; break;
    case SET_CONFIRM_TRASH: cfg->confirm_trash = !cfg->confirm_trash; break;
    case SET_OPEN_AFTER_CREATE: cfg->open_after_create = !cfg->open_after_create; break;
    case SET_PURGE_DAYS:
        cfg->purge_days = step_cycle(s_purge_day_steps, sizeof(s_purge_day_steps) / sizeof(int), cfg->purge_days, step);
        break;
    case SET_PLUGINS_ENABLED:
        set_plugin_system_enabled(cfg, !is_plugin_system_enabled(cfg));
        break;
    case SET_PLUGIN_SOURCE:
        cfg->plugin_source = (PluginSource)(((int)cfg->plugin_source + (step > 0 ? 1 : 2)) % 3);
        break;
    case SET_PLUGIN_CONFIRM_RUN:
        cfg->plugin_confirm_run = !cfg->plugin_confirm_run;
        g_plugin_confirm_run = cfg->plugin_confirm_run;
        break;
    case SET_PLUGIN_CONFIRM_INSTALL: cfg->plugin_confirm_install = !cfg->plugin_confirm_install; break;
    case SET_PLUGIN_SCAN_CWD: cfg->plugin_scan_cwd = !cfg->plugin_scan_cwd; break;
    case SET_PLUGIN_REPO:
    case SET_PLUGIN_BRANCH: {
        bool repo = id == SET_PLUGIN_REPO;
        char value[INPUT_MAX];
        if (prompt_text(state, repo ? "GitHub repo (owner/name)" : "GitHub branch", value, sizeof(value))) {
            if (!is_safe_url_part(value) || (repo && strchr(value, '/') == NULL)) {
                snprintf(state->status, sizeof(state->status), "\"%s\" is not a valid %s", value, repo ? "owner/name" : "branch name");
            } else if (repo) {
                snprintf(cfg->plugin_repo, sizeof(cfg->plugin_repo), "%s", value);
            } else {
                snprintf(cfg->plugin_branch, sizeof(cfg->plugin_branch), "%s", value);
            }
        }
        break;
    }
    case SET_EDIT_KEYS:
        if (dir == 0) keybindings_config_flow(state, cfg);
        return;
    case SET_RESET_KEYS:
        if (dir == 0 && prompt_confirm(state, "Reset every core key to its default?")) {
            set_default_keybindings(cfg);
            snprintf(state->status, sizeof(state->status), "keybindings reset");
        } else {
            return;
        }
        break;
    }

    save_config(cfg);
}

static void render_settings_ui(AppState *state, const AppConfig *cfg, int tab, size_t selected) {
    clear_owned_region(state);

    char line[1024];
    snprintf(line, sizeof(line), ANSI_BOLD "blob" ANSI_RESET "%s  \xc2\xb7  settings" ANSI_RESET, g_theme.help);
    render_line(state, line);
    render_line(state, "");

    char tabs[512] = "  ";
    char underline[512] = "  ";
    for (int t = 0; t < SETTING_TAB_COUNT; t++) {
        size_t len = strlen(tabs);
        size_t name_len = strlen(s_setting_tabs[t]);
        if (t == tab) {
            snprintf(tabs + len, sizeof(tabs) - len, ANSI_BOLD "%s%s" ANSI_RESET "   ", g_theme.selected, s_setting_tabs[t]);
        } else {
            snprintf(tabs + len, sizeof(tabs) - len, "%s%s" ANSI_RESET "   ", g_theme.help, s_setting_tabs[t]);
        }
        size_t ulen = strlen(underline);
        if (t == tab) {
            ulen += (size_t)snprintf(underline + ulen, sizeof(underline) - ulen, "%s", g_theme.selected);
            for (size_t i = 0; i < name_len && ulen + 4 < sizeof(underline); i++) {
                memcpy(underline + ulen, "\xe2\x94\x80", 3);
                ulen += 3;
            }
            ulen += (size_t)snprintf(underline + ulen, sizeof(underline) - ulen, ANSI_RESET "   ");
            underline[ulen] = '\0';
        } else {
            for (size_t i = 0; i < name_len + 3 && ulen + 2 < sizeof(underline); i++) {
                underline[ulen++] = ' ';
            }
            underline[ulen] = '\0';
        }
    }
    render_line(state, tabs);
    render_line(state, underline);

    const SettingDef *current = NULL;
    size_t row = 0;
    for (size_t i = 0; i < SETTING_COUNT; i++) {
        if (s_settings[i].tab != tab) continue;

        char value[INPUT_MAX];
        setting_value(cfg, s_settings[i].id, value, sizeof(value));
        char label[64];
        fit_column(label, sizeof(label), s_settings[i].label, 22);
        char shown[INPUT_MAX + 8];
        fit_column(shown, sizeof(shown), value, 40);

        if (row == selected) {
            current = &s_settings[i];
            snprintf(line, sizeof(line), "%s\xe2\x96\x8c" ANSI_RESET " " ANSI_BOLD "%s%s" ANSI_RESET "  %s%s" ANSI_RESET,
                     g_theme.selected, g_theme.selected, label, g_theme.title, shown);
        } else {
            snprintf(line, sizeof(line), "  %s  %s%s" ANSI_RESET, label, g_theme.title, shown);
        }
        render_line(state, line);
        row++;
    }

    render_line(state, "");
    if (current) {
        const char *text = current->help;
        char theme_help[160];
        if (current->id == SET_THEME) {
            const char *name = resolve_theme_name(cfg->theme_name);
            snprintf(theme_help, sizeof(theme_help), "Colour preset. Changes apply as you cycle (%zu themes).", s_theme_count);
            for (size_t i = 0; i < s_theme_count; i++) {
                if (strcmp(s_themes[i].name, name) == 0) {
                    snprintf(theme_help, sizeof(theme_help), "%s. Changes apply as you cycle (%zu/%zu).",
                             s_themes[i].description, i + 1, s_theme_count);
                }
            }
            text = theme_help;
        }
        int cols = terminal_columns();
        size_t width = cols > 4 ? (size_t)cols - 3 : 40;
        char help[512];
        fit_column(help, sizeof(help), text, width);
        size_t end = strlen(help);
        while (end > 0 && help[end - 1] == ' ') help[--end] = '\0';
        snprintf(line, sizeof(line), "%s  %s" ANSI_RESET, g_theme.help, help);
        render_line(state, line);
        render_line(state, "");
    }

    char hints[1024];
    hints[0] = '\0';
    append_hint(hints, sizeof(hints), "tab", "next section");
    append_hint(hints, sizeof(hints), "\xe2\x86\x91\xe2\x86\x93", "move");
    append_hint(hints, sizeof(hints), "\xe2\x86\x90\xe2\x86\x92", "change");
    append_hint(hints, sizeof(hints), "enter", "edit");
    append_hint(hints, sizeof(hints), "esc", "done");
    render_line(state, hints);

    if (state->status[0]) {
        render_line(state, "");
        snprintf(line, sizeof(line), "%s%s" ANSI_RESET, g_theme.status, state->status);
        render_line(state, line);
        state->status[0] = '\0';
    }
    fflush(stdout);
}

static void settings_flow(AppState *state, AppConfig *cfg) {
    int tab = 0;
    size_t selected = 0;

    for (;;) {
        size_t rows = 0;
        for (size_t i = 0; i < SETTING_COUNT; i++) {
            if (s_settings[i].tab == tab) rows++;
        }
        if (selected >= rows) selected = rows > 0 ? rows - 1 : 0;

        render_settings_ui(state, cfg, tab, selected);
        KeyEvent key = read_key();

        const SettingDef *def = NULL;
        size_t row = 0;
        for (size_t i = 0; i < SETTING_COUNT; i++) {
            if (s_settings[i].tab != tab) continue;
            if (row++ == selected) def = &s_settings[i];
        }

        if (key.type == KEY_ESCAPE || (key.type == KEY_CHAR && (key.ch == cfg->key_quit || key.ch == ','))) {
            break;
        } else if (key.type == KEY_CHAR && key.ch == '\t') {
            tab = (tab + 1) % SETTING_TAB_COUNT;
            selected = 0;
        } else if (key.type == KEY_BACKTAB) {
            tab = (tab + SETTING_TAB_COUNT - 1) % SETTING_TAB_COUNT;
            selected = 0;
        } else if (key.type == KEY_UP || (key.type == KEY_CHAR && key.ch == cfg->key_move_up)) {
            selected = selected > 0 ? selected - 1 : (rows > 0 ? rows - 1 : 0);
        } else if (key.type == KEY_DOWN || (key.type == KEY_CHAR && key.ch == cfg->key_move_down)) {
            selected = selected + 1 < rows ? selected + 1 : 0;
        } else if (def && (key.type == KEY_RIGHT || (key.type == KEY_CHAR && key.ch == 'l'))) {
            if (def->id != SET_EDITOR && def->id != SET_PLUGIN_REPO && def->id != SET_PLUGIN_BRANCH) {
                setting_change(state, cfg, def->id, 1);
            }
        } else if (def && (key.type == KEY_LEFT || (key.type == KEY_CHAR && key.ch == 'h'))) {
            if (def->id != SET_EDITOR && def->id != SET_PLUGIN_REPO && def->id != SET_PLUGIN_BRANCH) {
                setting_change(state, cfg, def->id, -1);
            }
        } else if (def && (key.type == KEY_ENTER || (key.type == KEY_CHAR && key.ch == ' '))) {
            setting_change(state, cfg, def->id, 0);
        }
    }

    clear_owned_region(state);
    load_notes(&state->notes, cfg);
    load_favorites_for_list(&state->notes, cfg);
    normalize_selection(state);
}

static void command_palette_flow(AppState *state, const AppConfig *cfg) {
    char command[INPUT_MAX];
    command[0] = '\0';
    if (!prompt_text(state, "Command", command, sizeof(command))) {
        return;
    }

    lower_ascii(command);

    if (strcmp(command, "new") == 0 || strcmp(command, "n") == 0) {
        create_note_flow(state, cfg);
    } else if (strcmp(command, "open") == 0 || strcmp(command, "edit") == 0) {
        open_selected_note(state, cfg);
    } else if (strcmp(command, "rename") == 0 || strcmp(command, "r") == 0) {
        rename_note_flow(state, cfg);
    } else if (strcmp(command, "trash") == 0 || strcmp(command, "delete") == 0 || strcmp(command, "d") == 0) {
        delete_note_flow(state, cfg);
    } else if (strcmp(command, "restore") == 0 || strcmp(command, "bin") == 0) {
        trash_viewer_flow(state, cfg);
    } else if (strcmp(command, "reminders") == 0 || strcmp(command, "remind") == 0) {
        show_reminders_flow(state, cfg);
    } else if (strcmp(command, "copy path") == 0 || strcmp(command, "copy") == 0) {
        copy_path_to_clipboard(state, cfg);
    } else if (strcmp(command, "settings") == 0 || strcmp(command, "config") == 0 || strcmp(command, "preferences") == 0) {
        settings_flow(state, (AppConfig *)cfg);
    } else if (strcmp(command, "plugins") == 0 || strcmp(command, "plugin") == 0) {
        plugin_manager_flow(state, cfg);
    } else if (strcmp(command, "keys") == 0 || strcmp(command, "keybindings") == 0 || strcmp(command, "bindings") == 0) {
        keybindings_config_flow(state, (AppConfig *)cfg);
    } else if (strcmp(command, "view") == 0 || strcmp(command, "preview") == 0) {
        quick_view_flow(state, cfg);
    } else if (strcmp(command, "doctor") == 0 || strcmp(command, "check") == 0) {
        doctor_flow(state, cfg);
    } else if (strcmp(command, "sort title") == 0 || strcmp(command, "sort name") == 0) {
        g_sort_mode = SORT_TITLE;
        snprintf(((AppConfig *)cfg)->sort_order, sizeof(((AppConfig *)cfg)->sort_order), "title");
        save_config(cfg);
        qsort(state->notes.items, state->notes.count, sizeof(*state->notes.items), note_cmp);
        normalize_selection(state);
        snprintf(state->status, sizeof(state->status), "Sort: title");
    } else if (strcmp(command, "sort mtime") == 0 || strcmp(command, "sort time") == 0) {
        g_sort_mode = SORT_MTIME;
        snprintf(((AppConfig *)cfg)->sort_order, sizeof(((AppConfig *)cfg)->sort_order), "mtime");
        save_config(cfg);
        qsort(state->notes.items, state->notes.count, sizeof(*state->notes.items), note_cmp);
        normalize_selection(state);
        snprintf(state->status, sizeof(state->status), "Sort: mtime");
    } else if (strcmp(command, "sort size") == 0) {
        g_sort_mode = SORT_SIZE;
        snprintf(((AppConfig *)cfg)->sort_order, sizeof(((AppConfig *)cfg)->sort_order), "size");
        save_config(cfg);
        qsort(state->notes.items, state->notes.count, sizeof(*state->notes.items), note_cmp);
        normalize_selection(state);
        snprintf(state->status, sizeof(state->status), "Sort: size");
    } else if (strcmp(command, "sort reverse") == 0 || strcmp(command, "reverse") == 0) {
        ((AppConfig *)cfg)->sort_reverse = !((AppConfig *)cfg)->sort_reverse;
        g_sort_reverse = ((AppConfig *)cfg)->sort_reverse;
        save_config(cfg);
        qsort(state->notes.items, state->notes.count, sizeof(*state->notes.items), note_cmp);
        normalize_selection(state);
        snprintf(state->status, sizeof(state->status), "Sort reverse: %s", g_sort_reverse ? "on" : "off");
    } else if (strcmp(command, "quit") == 0 || strcmp(command, "q") == 0) {
        state->running = false;
    } else if (!run_named_plugin(state, cfg, command)) {
        snprintf(state->status, sizeof(state->status), "unknown command: %s", command);
    }
}
#endif

static void undo_last_action(AppState *state, const AppConfig *cfg) {
    if (state->undo_count == 0) {
        snprintf(state->status, sizeof(state->status), "Nothing to undo");
        return;
    }

    UndoAction a = state->undo_stack[--state->undo_count];
    bool ok = false;

    switch (a.type) {
    case UNDO_TRASH:
        ok = rename(a.current_path, a.target_path) == 0;
        if (ok) {
            snprintf(state->status, sizeof(state->status), "Undid trash of \"%s\"", a.title);
        }
        break;
    case UNDO_RENAME:
        ok = rename(a.current_path, a.target_path) == 0;
        if (ok) {
            snprintf(state->status, sizeof(state->status), "Undid rename of \"%s\"", a.title);
        }
        break;
    default:
        break;
    }

    if (!ok) {
        snprintf(state->status, sizeof(state->status), "Undo failed: %s", strerror(errno));
        if (state->undo_count < MAX_UNDO) {
            state->undo_stack[state->undo_count++] = a;
        }
        return;
    }

    if (state->redo_count >= MAX_UNDO) {
        memmove(&state->redo_stack[0], &state->redo_stack[1],
                (MAX_UNDO - 1) * sizeof(UndoAction));
        state->redo_count = MAX_UNDO - 1;
    }
    state->redo_stack[state->redo_count++] = a;

    load_notes(&state->notes, cfg);
    load_favorites_for_list(&state->notes, cfg);
    normalize_selection(state);
}

static void redo_last_action(AppState *state, const AppConfig *cfg) {
    if (state->redo_count == 0) {
        snprintf(state->status, sizeof(state->status), "Nothing to redo");
        return;
    }

    UndoAction a = state->redo_stack[--state->redo_count];
    bool ok = false;

    switch (a.type) {
    case UNDO_TRASH:
        ok = rename(a.target_path, a.current_path) == 0;
        if (ok) {
            snprintf(state->status, sizeof(state->status), "Redid trash of \"%s\"", a.title);
        }
        break;
    case UNDO_RENAME:
        ok = rename(a.target_path, a.current_path) == 0;
        if (ok) {
            snprintf(state->status, sizeof(state->status), "Redid rename of \"%s\"", a.title);
        }
        break;
    default:
        break;
    }

    if (!ok) {
        snprintf(state->status, sizeof(state->status), "Redo failed: %s", strerror(errno));
        if (state->redo_count < MAX_UNDO) {
            state->redo_stack[state->redo_count++] = a;
        }
        return;
    }

    if (state->undo_count >= MAX_UNDO) {
        memmove(&state->undo_stack[0], &state->undo_stack[1],
                (MAX_UNDO - 1) * sizeof(UndoAction));
        state->undo_count = MAX_UNDO - 1;
    }
    state->undo_stack[state->undo_count++] = a;

    load_notes(&state->notes, cfg);
    load_favorites_for_list(&state->notes, cfg);
    normalize_selection(state);
}

#ifndef BLOB_TEST
static void move_selection_paged(AppState *state, int pages) {
    int n = pages * (int)VISIBLE_NOTES;
    if (n > 0) {
        for (int i = 0; i < n; i++) move_selection(state, 1);
    } else {
        for (int i = 0; i < -n; i++) move_selection(state, -1);
    }
}

static void jump_to_first_note(AppState *state) {
    for (size_t i = 0; i < state->notes.count; i++) {
        if (note_visible(state, i)) {
            state->selected = i;
            return;
        }
    }
}

static void jump_to_last_note(AppState *state) {
    for (size_t i = state->notes.count; i > 0; i--) {
        if (note_visible(state, i - 1)) {
            state->selected = i - 1;
            return;
        }
    }
}

static void handle_key(AppState *state, const AppConfig *cfg, KeyEvent key) {
    if (key.type == KEY_UP) {
        move_selection(state, -1);
        return;
    }
    if (key.type == KEY_DOWN) {
        move_selection(state, 1);
        return;
    }
    if (key.type == KEY_PGUP) {
        move_selection_paged(state, -1);
        return;
    }
    if (key.type == KEY_PGDN) {
        move_selection_paged(state, 1);
        return;
    }
    if (key.type == KEY_HOME) {
        jump_to_first_note(state);
        return;
    }
    if (key.type == KEY_END) {
        jump_to_last_note(state);
        return;
    }
    if (key.type == KEY_CTRL_U) {
        // Half-page scroll (vim style)
        for (int i = 0; i < (int)VISIBLE_NOTES / 2; i++) move_selection(state, -1);
        return;
    }
    if (key.type == KEY_CTRL_D) {
        for (int i = 0; i < (int)VISIBLE_NOTES / 2; i++) move_selection(state, 1);
        return;
    }
    if (key.type == KEY_ENTER) {
        open_selected_note(state, cfg);
        return;
    }

    if (key.type == KEY_CTRL_C) {
        state->running = false;
        return;
    }

    if (state->search_mode) {
        if (key.type == KEY_CHAR && key.ch == 'q') {
            state->running = false;
            return;
        }
        handle_search_key(state, key);
        return;
    }

    // Vim-style navigation (after search mode so j/k can be typed in search)
    if (key.type == KEY_CHAR && key.ch == cfg->key_move_up) {
        move_selection(state, -1);
        return;
    }
    if (key.type == KEY_CHAR && key.ch == cfg->key_move_down) {
        move_selection(state, 1);
        return;
    }

    if (key.type == KEY_CTRL_R) {
        show_reminders_flow(state, cfg);
        return;
    }
    if (key.type == KEY_CTRL_K) {
        keybindings_config_flow(state, (AppConfig *)cfg);
        return;
    }
    if (key.type == KEY_CTRL_O || (key.type == KEY_CHAR && key.ch == '?')) {
        state->show_help_expanded = !state->show_help_expanded;
        return;
    }
    if (key.type == KEY_CTRL_Z) {
        redo_last_action(state, cfg);
        return;
    }
    if (key.type == KEY_CTRL_T) {
        // Cycle sort order: mtime -> title -> size -> mtime
        if (g_sort_mode == SORT_MTIME) g_sort_mode = SORT_TITLE;
        else if (g_sort_mode == SORT_TITLE) g_sort_mode = SORT_SIZE;
        else g_sort_mode = SORT_MTIME;
        const char *name = g_sort_mode == SORT_TITLE ? "title" : g_sort_mode == SORT_SIZE ? "size" : "mtime";
        snprintf(((AppConfig *)cfg)->sort_order, sizeof(((AppConfig *)cfg)->sort_order), "%s", name);
        save_config(cfg);
        qsort(state->notes.items, state->notes.count, sizeof(*state->notes.items), note_cmp);
        normalize_selection(state);
        snprintf(state->status, sizeof(state->status), "Sort: %s", name);
        return;
    }

    if (key.type != KEY_CHAR) {
        return;
    }

    if (key.ch == cfg->key_quit) {
        state->running = false;
    } else if (key.ch == cfg->key_create) {
        create_note_flow(state, cfg);
    } else if (key.ch == cfg->key_rename) {
        rename_note_flow(state, cfg);
    } else if (key.ch == cfg->key_trash) {
        delete_note_flow(state, cfg);
    } else if (key.ch == cfg->key_trash_bin) {
        trash_viewer_flow(state, cfg);
    } else if (key.ch == cfg->key_copy) {
        copy_path_to_clipboard(state, cfg);
    } else if (key.ch == cfg->key_star) {
        toggle_favorite(state, cfg);
    } else if (key.ch == cfg->key_search) {
        state->search_mode = true;
        state->search[0] = '\0';
        normalize_selection(state);
    } else if (key.ch == ',') {
        settings_flow(state, (AppConfig *)cfg);
    } else if (key.ch == cfg->key_plugins) {
        plugin_manager_flow(state, cfg);
    } else if (key.ch == cfg->key_cmd) {
        command_palette_flow(state, cfg);
    } else if (key.ch == cfg->key_undo) {
        undo_last_action(state, cfg);
    } else if (key.ch == cfg->key_view) {
        quick_view_flow(state, cfg);
    } else if (key.ch == 'g') {
        jump_to_first_note(state);
    } else if (key.ch == 'G') {
        jump_to_last_note(state);
    } else {
        if (is_plugin_system_enabled(cfg) && state->notes.count > 0 && selected_is_visible(state)) {
            PluginList temp_plugins = {NULL, 0, 0};
            scan_addons_dir(&temp_plugins, cfg, cfg->addons_dir);
            if (cfg->plugin_scan_cwd) scan_addons_dir(&temp_plugins, cfg, "addons");
            mark_plugin_keybind_conflicts(&temp_plugins, cfg);

            for (size_t i = 0; i < temp_plugins.count; i++) {
                Plugin *p = &temp_plugins.items[i];
                if (p->is_compiled && !p->is_disabled && !p->has_keybind_conflict && p->keybind == key.ch) {
                    if (plugin_uses_workspace(p)) {
                        run_plugin_for_workspace(state, p, cfg);
                    } else {
                        run_plugin_on_note(state, p, state->notes.items[state->selected].path);
                    }
                    plugin_list_free(&temp_plugins);
                    load_notes(&state->notes, cfg);
                    normalize_selection(state);
                    return;
                }
            }
            plugin_list_free(&temp_plugins);
        }
    }
}
#endif

#ifndef BLOB_TEST
static void ui_loop(AppState *state, const AppConfig *cfg) {
    enable_raw_mode();

    while (state->running) {
        render_ui(state, cfg);
        KeyEvent key = read_key();
        handle_key(state, cfg, key);
    }

    clear_owned_region(state);
}
#endif

#ifndef BLOB_TEST
static int run_doctor_cli(const AppConfig *cfg) {
    char report[4096];
    int failed = build_doctor_report(cfg, report, sizeof(report));
    printf("%s", report);
    return failed;
}
#endif

#ifndef BLOB_TEST
int main(int argc, char **argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    atexit(restore_terminal_at_exit);
    setup_signal_handlers();

    AppConfig cfg;
    AppState state;
    memset(&cfg, 0, sizeof(cfg));
    memset(&state, 0, sizeof(state));

    state.running = true;

    init_paths(&cfg);
    load_config(&cfg);
    load_theme(&cfg);

    // Non-interactive doctor mode: blob doctor
    if (argc > 1 && strcmp(argv[1], "doctor") == 0) {
        return run_doctor_cli(&cfg);
    }

    // Trash auto-purge before loading the list
    if (cfg.purge_days > 0) {
        int purged = purge_old_trashed_notes(&cfg, cfg.purge_days);
        if (purged > 0) {
            snprintf(state.status, sizeof(state.status),
                     "Auto-purged %d trashed note(s) older than %d day(s)",
                     purged, cfg.purge_days);
        }
    }

    if (!load_notes(&state.notes, &cfg)) {
        fprintf(stderr, "blob: failed to load notes\n");
        return 1;
    }
    load_favorites_for_list(&state.notes, &cfg);

    // Session restore: select the last opened note if it still exists
    char session_note[PATH_MAX];
    if (cfg.restore_session && load_session(&cfg, session_note, sizeof(session_note))) {
        for (size_t i = 0; i < state.notes.count; i++) {
            if (strcmp(state.notes.items[i].path, session_note) == 0) {
                state.selected = i;
                break;
            }
        }
    }
    normalize_selection(&state);

    ui_loop(&state, &cfg);

    // Persist session on exit
    if (selected_is_visible(&state)) {
        save_session(&cfg, state.notes.items[state.selected].path);
    }

    exit_alt_screen();
    note_list_free(&state.notes);
    return 0;
}
#endif
