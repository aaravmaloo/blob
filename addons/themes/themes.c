// themes.c — built-in theme switcher for blob
// Workspace mode plugin.
// Receives the notes directory as argv[1].
// Derives the config path as <parent_of_notes>/config.
// Lists available themes and lets the user pick one.

#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <unistd.h>
#else
#include <windows.h>
#include <conio.h>
#endif

#define THEME_COUNT 5
#define PATH_MAX 4096
#define LINE_MAX 1024

static const char *theme_names[THEME_COUNT] = {
    "default", "dark", "light", "dracula", "solarized"
};

static const char *theme_descriptions[THEME_COUNT] = {
    "Cyan/green default theme",
    "Soft blue on dark background",
    "Blue/green for light terminals",
    "Purple/mint Dracula palette",
    "Teal/orange Solarized palette"
};

#ifdef _WIN32
static void enable_raw_mode(void) {
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;
    GetConsoleMode(h, &mode);
    mode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT);
    SetConsoleMode(h, mode);
}

static void disable_raw_mode(void) {
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;
    GetConsoleMode(h, &mode);
    mode |= ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT;
    SetConsoleMode(h, mode);
}

static int read_key(void) {
    int c = _getch();
    if (c == 0 || c == 224) {
        int ext = _getch();
        if (ext == 72) return -1;  // up
        if (ext == 80) return -2;  // down
        return 0;
    }
    return c;
}
#else
#include <signal.h>
#include <termios.h>
#include <sys/select.h>

static struct termios orig;
static volatile sig_atomic_t g_raw_enabled = 0;

static volatile sig_atomic_t g_signal_received = 0;

static void signal_handler(int sig) {
    (void)sig;
    if (g_signal_received) return;
    g_signal_received = 1;

    // Async-signal-safe terminal cleanup
    if (g_raw_enabled) {
        static const char restore[] = "\x1b[?25h\x1b[0m";
        write(STDOUT_FILENO, restore, sizeof(restore) - 1);
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig);
    }
    _exit(128 + sig);
}

static void setup_signal_handlers(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

static void enable_raw_mode(void) {
    tcgetattr(STDIN_FILENO, &orig);
    struct termios raw = orig;
    raw.c_iflag &= (tcflag_t)~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    raw.c_lflag &= (tcflag_t)~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    raw.c_cflag &= (tcflag_t)~(PARENB);
    raw.c_cflag |= CS8;
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    g_raw_enabled = 1;
}

static void disable_raw_mode(void) {
    if (!g_raw_enabled) return;
    g_raw_enabled = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig);
}

static int read_key(void) {
    char c = 0;
    if (read(STDIN_FILENO, &c, 1) != 1) return 0;

    if (c == '\x1b') {
        char seq[2];
        fd_set set;
        struct timeval tv;
        FD_ZERO(&set);
        FD_SET(STDIN_FILENO, &set);
        tv.tv_sec = 0;
        tv.tv_usec = 200000;

        if (select(STDIN_FILENO + 1, &set, NULL, NULL, &tv) <= 0 ||
            read(STDIN_FILENO, &seq[0], 1) != 1) {
            return 27; // escape
        }

        FD_ZERO(&set);
        FD_SET(STDIN_FILENO, &set);
        tv.tv_sec = 0;
        tv.tv_usec = 200000;

        if (select(STDIN_FILENO + 1, &set, NULL, NULL, &tv) <= 0 ||
            read(STDIN_FILENO, &seq[1], 1) != 1) {
            return 27;
        }
        if (seq[0] == '[') {
            if (seq[1] == 'A') return -1;  // up
            if (seq[1] == 'B') return -2;  // down
        }
        return 27;
    }

    if (c == '\r' || c == '\n') return 13; // enter
    if (c == 127 || c == '\b') return 8;   // backspace
    return (unsigned char)c;
}
#endif

static void clear_screen(void) {
    printf("\033[2J\033[H");
    fflush(stdout);
}

static void render_list(size_t sel, size_t count, const char **names,
                        const char **descs) {
#define BOX_W 60
    printf("\033[?25l");    // Top border: ╔═══ blob theme selector ══...══╗
    printf("╔═══ blob theme selector ");
    for (int i = 0; i < BOX_W - 26; i++) printf("═");
    printf("╗\n");

    // Spacer
    printf("║%*s║\n", BOX_W - 2, "");

    for (size_t i = 0; i < count; i++) {
        // Truncate description to 30 chars for alignment
        char desc_buf[32];
        size_t dlen = strlen(descs[i]);
        if (dlen > 30) {
            memcpy(desc_buf, descs[i], 30);
            desc_buf[30] = '\0';
        } else {
            snprintf(desc_buf, sizeof(desc_buf), "%s", descs[i]);
        }
        if (i == sel) {
            printf("║  \033[1;32m→ %-20s\033[0m  \033[2m%-30s\033[0m  ║\n",
                   names[i], desc_buf);
        } else {
            printf("║    %-20s  \033[2m%-30s\033[0m  ║\n",
                   names[i], desc_buf);
        }
    }

    // Bottom border
    printf("╚");
    for (int i = 0; i < BOX_W - 2; i++) printf("═");
    printf("╝\n");

    printf("\n\033[2m↑/↓ navigate  ENTER select  ESC cancel\033[0m\n");
    fflush(stdout);
#undef BOX_W
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    // Determine config path from argv[1] (notes directory)
    char config_path[PATH_MAX];
    if (argc >= 2 && argv[1][0]) {
        // argv[1] is the notes directory (e.g., ~/.local/share/blob/notes)
        // We need the parent directory for config
        snprintf(config_path, sizeof(config_path), "%s", argv[1]);

        // Strip trailing separator
        size_t len = strlen(config_path);
        while (len > 0 && (config_path[len - 1] == '/' || config_path[len - 1] == '\\')) {
            config_path[--len] = '\0';
        }

        // Find the last separator to get the parent
        char *sep = strrchr(config_path, '/');
#ifdef _WIN32
        if (!sep) sep = strrchr(config_path, '\\');
#endif
        if (sep) {
            *sep = '\0';
        }
        strncat(config_path, "/config", sizeof(config_path) - strlen(config_path) - 1);
    } else {
        // Fallback — try HOME
        const char *home = getenv("HOME");
        if (home) {
            snprintf(config_path, sizeof(config_path),
                     "%s/.local/share/blob/config", home);
        } else {
            snprintf(config_path, sizeof(config_path), "config");
        }
    }

    size_t sel = 0;

    // Read current theme from config
    char current_theme[32] = "default";
    FILE *cf = fopen(config_path, "r");
    if (cf) {
        char line[LINE_MAX];
        while (fgets(line, sizeof(line), cf)) {
            if (strncmp(line, "theme", 5) == 0) {
                char *eq = strchr(line, '=');
                if (eq) {
                    char *val = eq + 1;
                    while (*val && isspace((unsigned char)*val)) val++;
                    size_t vlen = strlen(val);
                    while (vlen > 0 && isspace((unsigned char)val[vlen - 1])) {
                        val[--vlen] = '\0';
                    }
                    if (*val) {
                        snprintf(current_theme, sizeof(current_theme), "%s", val);
                        // Find the index
                        for (size_t i = 0; i < THEME_COUNT; i++) {
                            if (strcmp(theme_names[i], current_theme) == 0) {
                                sel = i;
                                break;
                            }
                        }
                    }
                }
                break;
            }
        }
        fclose(cf);
    }

    atexit(disable_raw_mode);
#ifndef _WIN32
    setup_signal_handlers();
#endif

    enable_raw_mode();

    clear_screen();
    printf("Current theme: \033[1m%s\033[0m\n\n", current_theme);

    bool running = true;
    while (running) {
        render_list(sel, THEME_COUNT, theme_names, theme_descriptions);
        int key = read_key();

        switch (key) {
        case -1: // up
            if (sel > 0) sel--;
            break;
        case -2: // down
            if (sel + 1 < THEME_COUNT) sel++;
            break;
        case 13: // enter
            running = false;
            break;
        case 27: // escape
            clear_screen();
            disable_raw_mode();
            printf("\033[?25h");
            printf("Theme selection cancelled.\n");
            return 0;
        case 'q':
            clear_screen();
            disable_raw_mode();
            printf("\033[?25h");
            printf("Theme selection cancelled.\n");
            return 0;
        }

        // Move cursor back up for re-render
        // (Each render call does clear_screen + draw, so no cursor move needed)
    }

    // Apply the selection
    // Read existing config, replace theme line, write back
    char config_content[LINE_MAX * 16];
    size_t config_len = 0;
    config_content[0] = '\0';

    cf = fopen(config_path, "r");
    if (cf) {
        char line[LINE_MAX];
        while (fgets(line, sizeof(line), cf)) {
            if (strncmp(line, "theme", 5) == 0) {
                char new_line[LINE_MAX];
                snprintf(new_line, sizeof(new_line), "theme = %s\n", theme_names[sel]);
                size_t nl = strlen(new_line);
                if (config_len + nl < sizeof(config_content)) {
                    memcpy(config_content + config_len, new_line, nl + 1);
                    config_len += nl;
                }
            } else {
                size_t ll = strlen(line);
                if (config_len + ll < sizeof(config_content)) {
                    memcpy(config_content + config_len, line, ll + 1);
                    config_len += ll;
                }
            }
        }
        fclose(cf);
    }

    // If config was empty or had no theme line, add one
    if (config_len == 0 || strstr(config_content, "theme =") == NULL) {
        char new_line[LINE_MAX];
        snprintf(new_line, sizeof(new_line), "theme = %s\n", theme_names[sel]);
        size_t nl = strlen(new_line);
        if (config_len + nl < sizeof(config_content)) {
            memcpy(config_content + config_len, new_line, nl + 1);
            config_len += nl;
        }
    }

    cf = fopen(config_path, "w");
    if (cf) {
        fwrite(config_content, 1, config_len, cf);
        fclose(cf);
    }

    clear_screen();
    disable_raw_mode();
    printf("\033[?25h");
    printf("Theme set to \033[1m%s\033[0m.\n", theme_names[sel]);
    printf("Restart blob to apply.\n");
    return 0;
}
