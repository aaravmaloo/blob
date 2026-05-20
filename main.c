// blob.c

#define _DEFAULT_SOURCE

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifdef _WIN32

    #include <windows.h>
    #include <conio.h>
    #include <direct.h>
    #include <io.h>

    #define mkdir(path, mode) _mkdir(path)
    #define unlink _unlink
    #define PATH_SEP "\\"

#else

    #include <termios.h>
    #include <unistd.h>
    #include <sys/ioctl.h>

    #define PATH_SEP "/"

#endif

// ─────────────────────────────────────────────────────────────────────────────
// constants
// ─────────────────────────────────────────────────────────────────────────────

#define MAX_NOTES      1024
#define MAX_NAME_LEN   256
#define INPUT_BUF      512

// ─────────────────────────────────────────────────────────────────────────────
// ansi
// ─────────────────────────────────────────────────────────────────────────────

#define ANSI_RESET         "\x1b[0m"
#define ANSI_BOLD          "\x1b[1m"
#define ANSI_DIM           "\x1b[2m"

#define ANSI_RED           "\x1b[91m"
#define ANSI_CYAN          "\x1b[96m"

#define ANSI_BG_CYAN       "\x1b[46m"
#define ANSI_BLACK         "\x1b[30m"

#define ANSI_HIDE_CURSOR   "\x1b[?25l"
#define ANSI_SHOW_CURSOR   "\x1b[?25h"

// ─────────────────────────────────────────────────────────────────────────────
// types
// ─────────────────────────────────────────────────────────────────────────────

typedef struct {
    char data_dir[PATH_MAX];
    char notes_dir[PATH_MAX];
    char config_file[PATH_MAX];
    char editor[64];
} AppConfig;

typedef struct {
    char name[MAX_NAME_LEN];
    char path[PATH_MAX];
    time_t mtime;
} Note;

typedef struct {
    Note items[MAX_NOTES];
    size_t count;
} NoteList;

enum {
    KEY_NONE = 0,
    KEY_UP,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_ENTER,
    KEY_CHAR
};

typedef struct {
    int type;
    char ch;
} KeyEvent;

// ─────────────────────────────────────────────────────────────────────────────
// platform state
// ─────────────────────────────────────────────────────────────────────────────

#ifndef _WIN32

static struct termios original_termios;

#else

static DWORD original_console_mode;
static HANDLE hstdin;
static HANDLE hstdout;

#endif

// ─────────────────────────────────────────────────────────────────────────────
// terminal
// ─────────────────────────────────────────────────────────────────────────────

static void disable_raw_mode(void) {

#ifdef _WIN32

    SetConsoleMode(hstdin,
                   original_console_mode);

#else

    tcsetattr(STDIN_FILENO,
              TCSAFLUSH,
              &original_termios);

#endif

    printf(ANSI_SHOW_CURSOR ANSI_RESET);
    fflush(stdout);
}

static void enable_raw_mode(void) {

#ifdef _WIN32

    hstdin = GetStdHandle(STD_INPUT_HANDLE);
    hstdout = GetStdHandle(STD_OUTPUT_HANDLE);

    DWORD outmode = 0;

    GetConsoleMode(hstdout,
                   &outmode);

    outmode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;

    SetConsoleMode(hstdout,
                   outmode);

    GetConsoleMode(hstdin,
                   &original_console_mode);

    DWORD raw = original_console_mode;

    raw &= ~(ENABLE_ECHO_INPUT |
             ENABLE_LINE_INPUT);

    SetConsoleMode(hstdin,
                   raw);

#else

    tcgetattr(STDIN_FILENO,
              &original_termios);

    struct termios raw = original_termios;

    raw.c_lflag &= ~(ECHO |
                     ICANON |
                     ISIG);

    raw.c_iflag &= ~(IXON |
                     ICRNL);

    raw.c_oflag &= ~(OPOST);

    tcsetattr(STDIN_FILENO,
              TCSAFLUSH,
              &raw);

#endif

    atexit(disable_raw_mode);

    printf(ANSI_HIDE_CURSOR);
    fflush(stdout);
}

static void clear_screen(void) {
    printf("\x1b[2J\x1b[H");
    fflush(stdout);
}

static KeyEvent read_key(void) {

    KeyEvent ev = { KEY_NONE, 0 };

#ifdef _WIN32

    int c = _getch();

    if (c == 13) {
        ev.type = KEY_ENTER;
        return ev;
    }

    if (c == 0 || c == 224) {

        int arrow = _getch();

        switch (arrow) {

            case 72:
                ev.type = KEY_UP;
                return ev;

            case 80:
                ev.type = KEY_DOWN;
                return ev;

            case 75:
                ev.type = KEY_LEFT;
                return ev;

            case 77:
                ev.type = KEY_RIGHT;
                return ev;
        }
    }

    ev.type = KEY_CHAR;
    ev.ch = (char)c;

    return ev;

#else

    char c;

    if (read(STDIN_FILENO,
             &c,
             1) != 1)
        return ev;

    if (c == '\r' || c == '\n') {
        ev.type = KEY_ENTER;
        return ev;
    }

    if (c == '\x1b') {

        char seq[2];

        if (read(STDIN_FILENO,
                 &seq[0],
                 1) != 1)
            return ev;

        if (read(STDIN_FILENO,
                 &seq[1],
                 1) != 1)
            return ev;

        if (seq[0] == '[') {

            switch (seq[1]) {

                case 'A':
                    ev.type = KEY_UP;
                    return ev;

                case 'B':
                    ev.type = KEY_DOWN;
                    return ev;

                case 'C':
                    ev.type = KEY_RIGHT;
                    return ev;

                case 'D':
                    ev.type = KEY_LEFT;
                    return ev;
            }
        }
    }

    ev.type = KEY_CHAR;
    ev.ch = c;

    return ev;

#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// filesystem
// ─────────────────────────────────────────────────────────────────────────────

static void ensure_dir(const char *path) {

    struct stat st;

    if (stat(path, &st) == -1) {
        mkdir(path, 0755);
    }
}

static void init_paths(AppConfig *cfg) {

#ifdef _WIN32

    const char *home = getenv("USERPROFILE");

    snprintf(cfg->data_dir,
             sizeof(cfg->data_dir),
             "%s\\AppData\\Local\\blob",
             home);

    snprintf(cfg->editor,
             sizeof(cfg->editor),
             "%s",
             getenv("EDITOR") ? getenv("EDITOR") : "nvim");

#else

    const char *home = getenv("HOME");

    snprintf(cfg->data_dir,
             sizeof(cfg->data_dir),
             "%s/.local/share/blob",
             home);

    snprintf(cfg->editor,
             sizeof(cfg->editor),
             "%s",
             getenv("EDITOR") ? getenv("EDITOR") : "vim");

#endif

    snprintf(cfg->notes_dir,
             sizeof(cfg->notes_dir),
             "%s" PATH_SEP "notes",
             cfg->data_dir);

    snprintf(cfg->config_file,
             sizeof(cfg->config_file),
             "%s" PATH_SEP "config.json",
             cfg->data_dir);

    ensure_dir(cfg->data_dir);
    ensure_dir(cfg->notes_dir);
}

// ─────────────────────────────────────────────────────────────────────────────
// notes
// ─────────────────────────────────────────────────────────────────────────────

static int note_cmp(const void *a,
                    const void *b) {

    const Note *na = a;
    const Note *nb = b;

    return (nb->mtime - na->mtime);
}

static void load_notes(NoteList *list,
                       const AppConfig *cfg) {

    list->count = 0;

    DIR *dir = opendir(cfg->notes_dir);

    if (!dir)
        return;

    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {

        if (!strstr(entry->d_name, ".md"))
            continue;

        if (list->count >= MAX_NOTES)
            break;

        Note *note = &list->items[list->count];

        snprintf(note->name,
                 sizeof(note->name),
                 "%s",
                 entry->d_name);

        note->name[strlen(note->name) - 3] = '\0';

        snprintf(note->path,
                 sizeof(note->path),
                 "%s" PATH_SEP "%s",
                 cfg->notes_dir,
                 entry->d_name);

        struct stat st;

        if (stat(note->path, &st) == 0) {
            note->mtime = st.st_mtime;
        }

        list->count++;
    }

    closedir(dir);

    qsort(list->items,
          list->count,
          sizeof(Note),
          note_cmp);
}

static void sanitize_filename(char *s) {

    while (*s) {

        if (*s == '/' || *s == '\\')
            *s = '-';

        s++;
    }
}

static bool create_note(const AppConfig *cfg,
                        const char *name) {

    char clean[MAX_NAME_LEN];

    snprintf(clean,
             sizeof(clean),
             "%s",
             name);

    sanitize_filename(clean);

    if (!strstr(clean, ".md")) {

        strncat(clean,
                ".md",
                sizeof(clean) - strlen(clean) - 1);
    }

    char path[PATH_MAX];

    snprintf(path,
             sizeof(path),
             "%s" PATH_SEP "%s",
             cfg->notes_dir,
             clean);

    FILE *f = fopen(path, "a");

    if (!f)
        return false;

    fclose(f);

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// editor
// ─────────────────────────────────────────────────────────────────────────────

static void open_note(const AppConfig *cfg,
                      const Note *note) {

    disable_raw_mode();

#ifdef _WIN32

    STARTUPINFO si;
    PROCESS_INFORMATION pi;

    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));

    si.cb = sizeof(si);

    char cmd[PATH_MAX + 128];

    snprintf(cmd,
             sizeof(cmd),
             "%s \"%s\"",
             cfg->editor,
             note->path);

    if (CreateProcess(
            NULL,
            cmd,
            NULL,
            NULL,
            TRUE,
            0,
            NULL,
            NULL,
            &si,
            &pi)) {

        WaitForSingleObject(pi.hProcess,
                            INFINITE);

        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

#else

    char cmd[PATH_MAX + 128];

    snprintf(cmd,
             sizeof(cmd),
             "%s \"%s\"",
             cfg->editor,
             note->path);

    system(cmd);

#endif

    enable_raw_mode();
}

// ─────────────────────────────────────────────────────────────────────────────
// ui
// ─────────────────────────────────────────────────────────────────────────────

static void render(const AppConfig *cfg,
                   const NoteList *notes,
                   size_t selected,
                   const char *status) {

    clear_screen();

    printf("  " ANSI_BOLD ANSI_CYAN "blob" ANSI_RESET);
    printf("  " ANSI_DIM "%s" ANSI_RESET "\n\n",
           cfg->editor);

    if (notes->count == 0) {

        printf("  " ANSI_DIM
               "no notes - press n to create one"
               ANSI_RESET "\n");

    } else {

        for (size_t i = 0; i < notes->count; i++) {

            const Note *note = &notes->items[i];

            if (i == selected) {

                printf("  "
                       ANSI_BOLD ANSI_BG_CYAN ANSI_BLACK
                       " %s "
                       ANSI_RESET "\n",
                       note->name);

            } else {

                printf("  "
                       ANSI_DIM "*" ANSI_RESET
                       " %s\n",
                       note->name);
            }
        }
    }

    printf("\n  " ANSI_DIM
           "[UP/DOWN] navigate  enter:open  n:new  d:delete  q:quit"
           ANSI_RESET "\n");

    if (status && *status) {

        printf("\n  "
               ANSI_RED "%s"
               ANSI_RESET "\n",
               status);
    }

    fflush(stdout);
}

// ─────────────────────────────────────────────────────────────────────────────
// prompt
// ─────────────────────────────────────────────────────────────────────────────

static void prompt_input(const char *prompt,
                         char *buf,
                         size_t size) {

    disable_raw_mode();

    printf("%s",
           prompt);

    fflush(stdout);

    if (fgets(buf,
              size,
              stdin)) {

        buf[strcspn(buf, "\n")] = '\0';
    }

    enable_raw_mode();
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

int main(void) {

#ifdef _WIN32

    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

#endif

    AppConfig cfg;
    NoteList notes;

    init_paths(&cfg);

    load_notes(&notes,
               &cfg);

    enable_raw_mode();

    size_t selected = 0;

    char status[256] = {0};

    while (1) {

        if (notes.count == 0)
            selected = 0;

        else if (selected >= notes.count)
            selected = notes.count - 1;

        render(&cfg,
               &notes,
               selected,
               status);

        status[0] = '\0';

        KeyEvent key = read_key();

        if (key.type == KEY_CHAR) {

            switch (tolower(key.ch)) {

                case 'q':
                    return 0;

                case 'n': {

                    char name[INPUT_BUF];

                    render(&cfg,
                           &notes,
                           selected,
                           NULL);

                    prompt_input("\n  new note name: ",
                                 name,
                                 sizeof(name));

                    if (strlen(name) > 0) {

                        if (!create_note(&cfg,
                                         name)) {

                            snprintf(status,
                                     sizeof(status),
                                     "failed to create note");
                        }

                        load_notes(&notes,
                                   &cfg);
                    }

                    break;
                }

                case 'd': {

                    if (notes.count == 0)
                        break;

                    char confirm[16];
                    char path[PATH_MAX];

                    snprintf(path,
                             sizeof(path),
                             "%s",
                             notes.items[selected].path);

                    prompt_input("\n  delete note? [y/N]: ",
                                 confirm,
                                 sizeof(confirm));

                    if (tolower(confirm[0]) == 'y') {

                        unlink(path);

                        load_notes(&notes,
                                   &cfg);

                        if (selected > 0)
                            selected--;
                    }

                    break;
                }
            }
        }

        else if (key.type == KEY_UP) {

            if (selected > 0)
                selected--;
        }

        else if (key.type == KEY_DOWN) {

            if (selected + 1 < notes.count)
                selected++;
        }

        else if (key.type == KEY_ENTER) {

            if (notes.count > 0) {

                open_note(&cfg,
                          &notes.items[selected]);

                load_notes(&notes,
                           &cfg);
            }
        }
    }

    return 0;
}
