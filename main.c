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

typedef struct {
    char data_dir[PATH_MAX];
    char notes_dir[PATH_MAX];
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
    KEY_RIGHT
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

    ensure_dir(cfg->data_dir);
    ensure_dir(cfg->notes_dir);
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

        if (isalnum(ch)) {
            if (pending_dash && out > 0 && out + 1 < slug_size) {
                slug[out++] = '-';
            }
            slug[out++] = (char)tolower(ch);
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

static void open_selected_note(AppState *state, const AppConfig *cfg) {
    if (state->notes.count == 0 || !selected_is_visible(state)) {
        return;
    }

    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s", state->notes.items[state->selected].path);

    if (!open_path_in_editor(state, cfg, path)) {
        snprintf(state->status, sizeof(state->status), "failed to launch editor");
    }

    load_notes(&state->notes, cfg);
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
    default:
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
