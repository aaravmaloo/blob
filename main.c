// blob.c

#define _DEFAULT_SOURCE

#include <ctype.h>
#include <errno.h>
#include <limits.h>
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
#define VISIBLE_NOTES 12

#define ANSI_RESET "\x1b[0m"
#define ANSI_BOLD "\x1b[1m"
#define ANSI_DIM "\x1b[2m"
#define ANSI_RED "\x1b[31m"
#define ANSI_HIDE_CURSOR "\x1b[?25l"
#define ANSI_SHOW_CURSOR "\x1b[?25h"
#define ANSI_CLEAR_LINE "\x1b[2K"

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
    char editor[INPUT_MAX];
} AppConfig;

typedef struct {
    char title[TITLE_MAX];
    char filename[TITLE_MAX];
    char path[PATH_MAX];
    time_t mtime;
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
    size_t rendered_lines;
    char status[INPUT_MAX];
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
    KEY_CTRL_P
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
    raw.c_lflag &= (tcflag_t) ~(ECHO | ICANON);
    raw.c_iflag &= (tcflag_t) ~(IXON | ICRNL);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
#endif

    printf(ANSI_HIDE_CURSOR);
    fflush(stdout);
    raw_enabled = true;
}

#ifndef BLOB_TEST
static void restore_terminal_at_exit(void) {
    disable_raw_mode();
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
    if (c == 0 || c == 224) {
        int ext = _getch();
        if (ext == 72) key.type = KEY_UP;
        else if (ext == 80) key.type = KEY_DOWN;
        else if (ext == 75) key.type = KEY_LEFT;
        else if (ext == 77) key.type = KEY_RIGHT;
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
    if (c == '\x1b') {
        char seq[2];

        fd_set set;
        struct timeval timeout;
        FD_ZERO(&set);
        FD_SET(STDIN_FILENO, &set);
        timeout.tv_sec = 0;
        timeout.tv_usec = 30000;

        if (select(STDIN_FILENO + 1, &set, NULL, NULL, &timeout) <= 0 ||
            read(STDIN_FILENO, &seq[0], 1) != 1) {
            key.type = KEY_ESCAPE;
            return key;
        }

        FD_ZERO(&set);
        FD_SET(STDIN_FILENO, &set);
        timeout.tv_sec = 0;
        timeout.tv_usec = 30000;

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

    printf("\r");
    for (size_t i = 0; i < state->rendered_lines; i++) {
        printf("\x1b[1A" ANSI_CLEAR_LINE "\r");
    }
    fflush(stdout);
    state->rendered_lines = 0;
}

static bool ensure_dir(const char *path) {
    struct stat st;

    if (stat(path, &st) == 0) {
        return STAT_ISDIR(st.st_mode);
    }

    return mkdir(path, 0755) == 0 || errno == EEXIST;
}

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

    ensure_dir(cfg->data_dir);
    ensure_dir(cfg->notes_dir);
    ensure_dir(cfg->addons_dir);
}
#endif

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

    if (left->mtime < right->mtime) return 1;
    if (left->mtime > right->mtime) return -1;
    return strcmp(left->title, right->title);
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

static bool create_note_file(const AppConfig *cfg, const char *title, char *path, size_t path_size) {
    char slug[TITLE_MAX];
    sanitize_slug(title, slug, sizeof(slug));
    unique_note_path(cfg, slug, path, path_size);

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
    printf(ANSI_CLEAR_LINE "%s\n", text);
    state->rendered_lines++;
}

#ifndef BLOB_TEST
static void render_ui(AppState *state) {
    normalize_selection(state);
    clear_owned_region(state);

    render_line(state, ANSI_BOLD "blob" ANSI_RESET);
    render_line(state, "");

    if (state->search_mode || state->search[0]) {
        char line[SEARCH_MAX + 16];
        snprintf(line, sizeof(line), "Search: %s", state->search);
        render_line(state, line);
        render_line(state, "");
    }

    size_t shown = 0;
    size_t total_visible = visible_count(state);

    if (state->notes.count == 0) {
        render_line(state, ANSI_DIM "no notes yet" ANSI_RESET);
    } else if (total_visible == 0) {
        render_line(state, ANSI_DIM "no matching notes" ANSI_RESET);
    } else {
        size_t start = first_rendered_note(state);
        for (size_t i = start; i < state->notes.count && shown < VISIBLE_NOTES; i++) {
            if (!note_visible(state, i)) {
                continue;
            }

            char line[TITLE_MAX + 16];
            snprintf(line, sizeof(line), "%s %s",
                     i == state->selected ? ">" : " ",
                     state->notes.items[i].title);
            render_line(state, line);
            shown++;
        }
    }

    render_line(state, "");
    render_line(state, ANSI_DIM "────────────────────────────────" ANSI_RESET);
    render_line(state, "");

    if (state->search_mode) {
        render_line(state, ANSI_DIM "[ENTER] open" ANSI_RESET);
        render_line(state, ANSI_DIM "[ESC] clear search" ANSI_RESET);
        render_line(state, ANSI_DIM "[q] quit" ANSI_RESET);
    } else {
        render_line(state, ANSI_DIM "[ENTER] open" ANSI_RESET);
        render_line(state, ANSI_DIM "[n] new" ANSI_RESET);
        render_line(state, ANSI_DIM "[d] delete" ANSI_RESET);
        render_line(state, ANSI_DIM "[/] search" ANSI_RESET);
        render_line(state, ANSI_DIM "[p] plugins" ANSI_RESET);
        render_line(state, ANSI_DIM "[q] quit" ANSI_RESET);
    }

    if (state->status[0]) {
        render_line(state, "");
        char status_line[INPUT_MAX + 16];
        snprintf(status_line, sizeof(status_line), ANSI_RED "%s" ANSI_RESET, state->status);
        render_line(state, status_line);
        state->status[0] = '\0';
    }

    fflush(stdout);
}
#endif

static bool prompt_text(AppState *state, const char *label, char *buffer, size_t buffer_size) {
    clear_owned_region(state);
    disable_raw_mode();

    printf("? %s\n> ", label);
    fflush(stdout);

    bool ok = fgets(buffer, (int)buffer_size, stdin) != NULL;
    if (ok) {
        buffer[strcspn(buffer, "\r\n")] = '\0';
    }

    enable_raw_mode();
    return ok && buffer[0] != '\0';
}

static bool prompt_confirm(AppState *state, const char *message) {
    clear_owned_region(state);
    printf("? %s (y/N)", message);
    fflush(stdout);

    KeyEvent key = read_key();
    bool confirmed = key.type == KEY_CHAR && (key.ch == 'y' || key.ch == 'Y');
    printf("%s\n", confirmed ? " y" : " N");
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

    bool ok = CreateProcessA(NULL, command, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi) != 0;
    if (ok) {
        WaitForSingleObject(pi.hProcess, INFINITE);
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
    bool ok = pid >= 0;

    if (pid == 0) {
        execvp(argv[0], argv);
        _exit(127);
    } else if (pid > 0) {
        int status = 0;
        while (waitpid(pid, &status, 0) == -1 && errno == EINTR) {
        }
    }
#endif

    enable_raw_mode();
    state->rendered_lines = 0;
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

    clear_owned_region(state);
    disable_raw_mode();
    printf("Created:\n%s\n\n", path);
    fflush(stdout);
    enable_raw_mode();

    open_path_in_editor(state, cfg, path);
    load_notes(&state->notes, cfg);
    normalize_selection(state);
}

static void delete_note_flow(AppState *state, const AppConfig *cfg) {
    (void)cfg;

    if (state->notes.count == 0 || !selected_is_visible(state)) {
        return;
    }

    Note selected = state->notes.items[state->selected];
    char message[TITLE_MAX + 32];
    snprintf(message, sizeof(message), "Delete \"%s\"?", selected.title);

    if (!prompt_confirm(state, message)) {
        return;
    }

    size_t previous = state->selected;
    if (unlink(selected.path) != 0) {
        snprintf(state->status, sizeof(state->status), "failed to delete note: %s", strerror(errno));
        return;
    }

    load_notes(&state->notes, cfg);
    state->selected = previous;
    normalize_selection(state);
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

typedef struct {
    char name[PLUGIN_NAME_MAX];
    char authors[PLUGIN_AUTH_MAX];
    char description[PLUGIN_DESC_MAX];
    char keybind;
    char dir_path[PATH_MAX];
    char c_path[PATH_MAX];
    char exe_path[PATH_MAX];
    bool is_compiled;
    bool is_remote;
    bool is_disabled;
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
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s" PATH_SEP "disabled_plugins", cfg->data_dir);
    
    char names[32][64];
    size_t count = 0;
    FILE *f = fopen(path, "r");
    if (f) {
        char line[128];
        while (fgets(line, sizeof(line), f) && count < 32) {
            size_t len = strlen(line);
            while (len > 0 && isspace((unsigned char)line[len - 1])) {
                line[--len] = '\0';
            }
            if (line[0] && strcmp(line, name) != 0) {
                snprintf(names[count++], 64, "%s", line);
            }
        }
        fclose(f);
    }

    if (disabled) {
        if (count < 32) {
            snprintf(names[count++], 64, "%s", name);
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

static bool parse_plugin_readme(const char *readme_path, Plugin *plugin) {
    FILE *f = fopen(readme_path, "r");
    if (!f) return false;

    char line[512];
    plugin->name[0] = '\0';
    plugin->authors[0] = '\0';
    plugin->description[0] = '\0';
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
            char *p = strchr(line, '=');
            if (p) {
                p++;
                while (*p && (*p == ' ' || *p == '{' || *p == '"' || *p == '\'' || *p == '[')) p++;
                char *end = p + strlen(p);
                while (end > p && (end[-1] == ' ' || end[-1] == '}' || end[-1] == '"' || end[-1] == '\'' || end[-1] == ']')) end--;
                *end = '\0';
                snprintf(plugin->authors, sizeof(plugin->authors), "%s", p);
            }
        } else if (strstr(line, "[description] =") != NULL) {
            char *p = strchr(line, '=');
            if (p) {
                p++;
                while (*p && (*p == ' ' || *p == '<' || *p == '"' || *p == '\'')) p++;
                char *end = p + strlen(p);
                while (end > p && (end[-1] == ' ' || end[-1] == '>' || end[-1] == '"' || end[-1] == '\'')) end--;
                *end = '\0';
                snprintf(plugin->description, sizeof(plugin->description), "%s", p);
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

static void fetch_remote_plugins(AppState *state, const AppConfig *cfg, PluginList *list) {
    clear_owned_region(state);
    disable_raw_mode();
    enter_alt_screen();
    printf("Fetching remote plugin repository index...\n");
    fflush(stdout);

    char temp_index[PATH_MAX];
    snprintf(temp_index, sizeof(temp_index), "%s" PATH_SEP "temp_addons.txt", cfg->data_dir);

    char cmd[PATH_MAX * 2 + 128];
    snprintf(cmd, sizeof(cmd), "curl -s -f -L \"https://raw.githubusercontent.com/aaravmaloo/blob/master/addons/addons.txt\" -o \"%s\"", temp_index);
    
    int ret = system(cmd);
    if (ret != 0) {
        printf("Error: failed to fetch remote plugins (network error or curl missing).\n");
        printf("Press any key to continue...");
        fflush(stdout);
        read_key();
        exit_alt_screen();
        enable_raw_mode();
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
        if (len == 0) continue;

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
                snprintf(cmd, sizeof(cmd), "curl -s -f -L \"https://raw.githubusercontent.com/aaravmaloo/blob/master/addons/%s/%s.c\" -o \"%s\"", line, line, temp_c);
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
        
        snprintf(cmd, sizeof(cmd), "curl -s -f -L \"https://raw.githubusercontent.com/aaravmaloo/blob/master/addons/%s/README.md\" -o \"%s\"", line, temp_readme);
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

static bool compile_plugin(AppState *state, const AppConfig *cfg, Plugin *plugin) {
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
        copy_file(plugin->c_path, new_c);

        char old_readme[PATH_MAX];
        snprintf(old_readme, sizeof(old_readme), "%s" PATH_SEP "README.md", plugin->dir_path);
        char new_readme[PATH_MAX];
        snprintf(new_readme, sizeof(new_readme), "%s" PATH_SEP "README.md", new_dir);
        copy_file(old_readme, new_readme);

        // Update paths to point to the data directory version
        snprintf(plugin->dir_path, sizeof(plugin->dir_path), "%s", new_dir);
        snprintf(plugin->c_path, sizeof(plugin->c_path), "%s", new_c);
#ifdef _WIN32
        snprintf(plugin->exe_path, sizeof(plugin->exe_path), "%s" PATH_SEP "%s.exe", plugin->dir_path, plugin->name);
#else
        snprintf(plugin->exe_path, sizeof(plugin->exe_path), "%s" PATH_SEP "%s", plugin->dir_path, plugin->name);
#endif
    }

    ensure_dir(plugin->dir_path);

    char cmd[PATH_MAX * 2 + 128];

    if (plugin->is_remote || plugin->update_available) {
        printf("Downloading plugin source files...\n");
        fflush(stdout);
        
        snprintf(cmd, sizeof(cmd), "curl -s -f -L \"https://raw.githubusercontent.com/aaravmaloo/blob/master/addons/%s/%s.c\" -o \"%s\"", plugin->name, plugin->name, plugin->c_path);
        if (system(cmd) != 0) {
            printf("Error: failed to download C source file.\nPress any key to continue...");
            fflush(stdout);
            read_key();
            exit_alt_screen();
            enable_raw_mode();
            return false;
        }

        char readme_path[PATH_MAX];
        snprintf(readme_path, sizeof(readme_path), "%s" PATH_SEP "README.md", plugin->dir_path);
        snprintf(cmd, sizeof(cmd), "curl -s -f -L \"https://raw.githubusercontent.com/aaravmaloo/blob/master/addons/%s/README.md\" -o \"%s\"", plugin->name, readme_path);
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
        read_key();
        exit_alt_screen();
        enable_raw_mode();
        return false;
    }

    printf("Successfully compiled and installed: %s\n", plugin->name);
    printf("Press any key to continue...");
    fflush(stdout);
    read_key();

    exit_alt_screen();
    enable_raw_mode();

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
    read_key();

    exit_alt_screen();
    enable_raw_mode();

    plugin->is_compiled = false;
    plugin->update_available = false;
}

static bool run_plugin_process(const char *exe_path, const char *note_path) {
#ifdef _WIN32
    char command[PATH_MAX * 2 + 16];
    snprintf(command, sizeof(command), "\"%s\" \"%s\"", exe_path, note_path);

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
        char *argv[] = {(char *)exe_path, (char *)note_path, NULL};
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

static void run_plugin_on_note(AppState *state, const Plugin *plugin, const char *note_path) {
    clear_owned_region(state);
    disable_raw_mode();
    enter_alt_screen();

    printf("Executing plugin: %s...\n", plugin->name);
    fflush(stdout);

    if (!run_plugin_process(plugin->exe_path, note_path)) {
        printf("\nPlugin execution failed.\nPress any key to continue...");
        fflush(stdout);
        read_key();
    }

    exit_alt_screen();
    enable_raw_mode();
    state->rendered_lines = 0;
}

static void render_plugin_ui(AppState *state, PluginList *plugins, size_t selected_plugin) {
    normalize_selection(state);
    clear_owned_region(state);

    render_line(state, ANSI_BOLD "blob: plugins manager" ANSI_RESET);
    render_line(state, "");

    if (plugins->count == 0) {
        render_line(state, ANSI_DIM "no plugins found" ANSI_RESET);
    } else {
        for (size_t i = 0; i < plugins->count; i++) {
            Plugin *p = &plugins->items[i];
            char line[256];
            char status[32] = "";
            if (p->is_disabled) {
                snprintf(status, sizeof(status), "[disabled]");
            } else if (p->is_compiled) {
                if (p->update_available) {
                    snprintf(status, sizeof(status), "[update available]");
                } else {
                    snprintf(status, sizeof(status), "[installed]");
                }
            } else if (p->is_remote) {
                snprintf(status, sizeof(status), "[remote]");
            } else {
                snprintf(status, sizeof(status), "[not compiled]");
            }

            snprintf(line, sizeof(line), "%s %-15s %s",
                     i == selected_plugin ? ">" : " ",
                     p->name, status);
            render_line(state, line);
        }
    }

    render_line(state, "");
    render_line(state, ANSI_DIM "────────────────────────────────" ANSI_RESET);
    render_line(state, "");

    if (plugins->count > 0 && selected_plugin < plugins->count) {
        Plugin *p = &plugins->items[selected_plugin];
        char line[512];
        snprintf(line, sizeof(line), "Author:      %s", p->authors[0] ? p->authors : "Unknown");
        render_line(state, line);
        snprintf(line, sizeof(line), "Description: %s", p->description[0] ? p->description : "None");
        render_line(state, line);
        snprintf(line, sizeof(line), "Keybind:     %c", p->keybind ? p->keybind : ' ');
        render_line(state, line);
        render_line(state, "");
    }

    render_line(state, ANSI_DIM "[ENTER] install/compile/update" ANSI_RESET);
    render_line(state, ANSI_DIM "[u] uninstall (delete binary)" ANSI_RESET);
    render_line(state, ANSI_DIM "[t] toggle enable/disable" ANSI_RESET);
    render_line(state, ANSI_DIM "[Ctrl+P] disable plugin system" ANSI_RESET);
    render_line(state, ANSI_DIM "[ESC/q] back to notes" ANSI_RESET);

    if (state->status[0]) {
        render_line(state, "");
        char status_line[INPUT_MAX + 16];
        snprintf(status_line, sizeof(status_line), ANSI_RED "%s" ANSI_RESET, state->status);
        render_line(state, status_line);
        state->status[0] = '\0';
    }

    fflush(stdout);
}

static void plugin_manager_flow(AppState *state, const AppConfig *cfg) {
    PluginList plugins = {NULL, 0, 0};

    scan_addons_dir(&plugins, cfg, cfg->addons_dir);
    scan_addons_dir(&plugins, cfg, "addons");

    bool check_remote = false;
    if (is_plugin_system_enabled(cfg)) {
        check_remote = prompt_confirm(state, "Access remote repository?");
    } else {
        if (prompt_confirm(state, "Plugin system is disabled. Enable it?")) {
            set_plugin_system_enabled(cfg, true);
            check_remote = prompt_confirm(state, "Access remote repository?");
        } else {
            plugin_list_free(&plugins);
            return;
        }
    }

    if (check_remote) {
        fetch_remote_plugins(state, cfg, &plugins);
    }

    size_t selected = 0;
    bool in_menu = true;

    while (in_menu) {
        render_plugin_ui(state, &plugins, selected);
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
        } else if (key.type == KEY_ENTER) {
            if (plugins.count > 0 && selected < plugins.count) {
                Plugin *p = &plugins.items[selected];
                if (!p->is_compiled || p->update_available) {
                    compile_plugin(state, cfg, p);
                } else {
                    snprintf(state->status, sizeof(state->status), "Already compiled. Press '%c' on notes list.", p->keybind);
                }
            }
        } else if (key.type == KEY_CHAR && key.ch == 'u') {
            if (plugins.count > 0 && selected < plugins.count) {
                Plugin *p = &plugins.items[selected];
                if (p->is_compiled || !p->is_remote) {
                    delete_plugin(state, cfg, &plugins, &selected, check_remote);
                }
            }
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
    state->rendered_lines = 0;
}

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
        scan_addons_dir(&temp_plugins, cfg, "addons");

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

    if (!open_path_in_editor(state, cfg, path)) {
        snprintf(state->status, sizeof(state->status), "failed to launch editor");
    }

    if (unlocked) {
        PluginList temp_plugins = {NULL, 0, 0};
        scan_addons_dir(&temp_plugins, cfg, cfg->addons_dir);
        scan_addons_dir(&temp_plugins, cfg, "addons");

        Plugin *lock_plugin = NULL;
        for (size_t i = 0; i < temp_plugins.count; i++) {
            if (strcmp(temp_plugins.items[i].name, "lock") == 0 && temp_plugins.items[i].is_compiled) {
                lock_plugin = &temp_plugins.items[i];
                break;
            }
        }

        if (lock_plugin) {
            printf("\nRe-encrypting note...\n");
            fflush(stdout);
            run_plugin_on_note(state, lock_plugin, path);
        }
        plugin_list_free(&temp_plugins);
    }

    printf("\033[2J\033[H");
    fflush(stdout);

    load_notes(&state->notes, cfg);
    normalize_selection(state);
}

#ifndef BLOB_TEST
static void handle_key(AppState *state, const AppConfig *cfg, KeyEvent key) {
    if (key.type == KEY_UP) {
        move_selection(state, -1);
        return;
    }
    if (key.type == KEY_DOWN) {
        move_selection(state, 1);
        return;
    }
    if (key.type == KEY_ENTER) {
        open_selected_note(state, cfg);
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

    if (key.type != KEY_CHAR) {
        return;
    }

    switch (key.ch) {
    case 'q':
        state->running = false;
        break;
    case 'n':
        create_note_flow(state, cfg);
        break;
    case 'd':
        delete_note_flow(state, cfg);
        break;
    case '/':
        state->search_mode = true;
        state->search[0] = '\0';
        normalize_selection(state);
        break;
    case 'p':
        plugin_manager_flow(state, cfg);
        break;
    default:
        if (is_plugin_system_enabled(cfg) && state->notes.count > 0 && selected_is_visible(state)) {
            PluginList temp_plugins = {NULL, 0, 0};
            scan_addons_dir(&temp_plugins, cfg, cfg->addons_dir);
            scan_addons_dir(&temp_plugins, cfg, "addons");

            for (size_t i = 0; i < temp_plugins.count; i++) {
                Plugin *p = &temp_plugins.items[i];
                if (p->is_compiled && !p->is_disabled && p->keybind == key.ch) {
                    run_plugin_on_note(state, p, state->notes.items[state->selected].path);
                    plugin_list_free(&temp_plugins);
                    load_notes(&state->notes, cfg);
                    normalize_selection(state);
                    return;
                }
            }
            plugin_list_free(&temp_plugins);
        }
        break;
    }
}
#endif

#ifndef BLOB_TEST
static void ui_loop(AppState *state, const AppConfig *cfg) {
    enable_raw_mode();

    while (state->running) {
        render_ui(state);
        KeyEvent key = read_key();
        handle_key(state, cfg, key);
    }

    clear_owned_region(state);
}
#endif

#ifndef BLOB_TEST
int main(void) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    atexit(restore_terminal_at_exit);

    AppConfig cfg;
    AppState state;
    memset(&cfg, 0, sizeof(cfg));
    memset(&state, 0, sizeof(state));

    state.running = true;

    init_paths(&cfg);
    if (!load_notes(&state.notes, &cfg)) {
        fprintf(stderr, "blob: failed to load notes\n");
        return 1;
    }

    ui_loop(&state, &cfg);
    note_list_free(&state.notes);
    return 0;
}
#endif
