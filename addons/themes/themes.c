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

#define PATH_MAX 4096
#define LINE_MAX 1024
#define VISIBLE_ROWS 10

// Colours are "#rrggbb" or a raw escape that follows the terminal's own palette
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
} ThemeDef;

#define THEME_COUNT 21

// Generated from the same table as blob's main.c so the preview matches what blob draws
static const ThemeDef themes[THEME_COUNT] = {
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

static const char *theme_aliases[][2] = {
    {"dark", "tokyo-night"},
    {"light", "catppuccin-latte"},
    {"solarized", "solarized-dark"},
};

static bool g_true_color = false;

static const char *resolve_theme_name(const char *name) {
    for (size_t i = 0; i < sizeof(theme_aliases) / sizeof(theme_aliases[0]); i++) {
        if (strcmp(theme_aliases[i][0], name) == 0) return theme_aliases[i][1];
    }
    return name;
}

static bool terminal_supports_true_color(void) {
    const char *colorterm = getenv("COLORTERM");
    if (colorterm && (strstr(colorterm, "truecolor") || strstr(colorterm, "24bit"))) return true;
    const char *term = getenv("TERM");
    if (term && (strstr(term, "kitty") || strstr(term, "alacritty") || strstr(term, "ghostty") ||
                 strstr(term, "wezterm") || strstr(term, "direct"))) return true;
    const char *program = getenv("TERM_PROGRAM");
    if (program && (strcmp(program, "iTerm.app") == 0 || strcmp(program, "WezTerm") == 0 ||
                    strcmp(program, "vscode") == 0 || strcmp(program, "ghostty") == 0)) return true;
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
    int cr = levels[idx[0]], cg = levels[idx[1]], cb = levels[idx[2]];
    long cube_dist = (long)(cr - r) * (cr - r) + (long)(cg - g) * (cg - g) + (long)(cb - b) * (cb - b);
    int avg = (r + g + b) / 3;
    int grey_i = avg < 8 ? 0 : (avg > 238 ? 23 : (avg - 8 + 5) / 10);
    int grey = 8 + grey_i * 10;
    long grey_dist = (long)(grey - r) * (grey - r) + (long)(grey - g) * (grey - g) + (long)(grey - b) * (grey - b);
    if (grey_dist < cube_dist) return 232 + grey_i;
    return 16 + 36 * idx[0] + 6 * idx[1] + idx[2];
}

// Returns the escape for a theme colour; buf must outlive the next call with the same buf
static const char *sgr(const char *color, char *buf, size_t size) {
    unsigned int r, g, b;
    if (color[0] == '#' && sscanf(color + 1, "%02x%02x%02x", &r, &g, &b) == 3) {
        if (g_true_color) snprintf(buf, size, "\x1b[38;2;%u;%u;%um", r, g, b);
        else snprintf(buf, size, "\x1b[38;5;%dm", rgb_to_xterm256((int)r, (int)g, (int)b));
        return buf;
    }
    return color;
}

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

// Draws a miniature of blob's note list in the highlighted theme
static void render_preview(size_t sel) {
    const ThemeDef *t = &themes[sel];
    const char *R = "\033[0m";
    char b1[32], b2[32], b3[32], b4[32], b5[32], b6[32];
    const char *T = sgr(t->title, b1, sizeof(b1));
    const char *Sel = sgr(t->selected, b2, sizeof(b2));
    const char *Se = sgr(t->search, b3, sizeof(b3));
    const char *H = sgr(t->help, b4, sizeof(b4));
    const char *Ts = sgr(t->timestamp, b5, sizeof(b5));
    const char *St = sgr(t->star, b6, sizeof(b6));

    printf("\n\033[2mpreview\033[0m\n\n");
    printf("  \033[1mblob\033[0m%s v1.5.0  \xc2\xb7  3 notes  \xc2\xb7  newest first%s\n\n", H, R);
    printf("  %s\xe2\x96\x8c%s %s\xe2\x98\x85%s \033[1m%s%-32s%s  %s%7s%s\n",
           Sel, R, St, R, Sel, "release checklist", R, Ts, "3h ago", R);
    printf("      %s%-32s%s  %s%7s%s\n", T, "standup notes", R, Ts, "1d ago", R);
    printf("      %s%-32s%s  %s%7s%s\n", T, "reading list", R, Ts, "Aug 05", R);
    printf("    %s1-3 of 3%s\n\n", Ts, R);
    printf("  %s/ search query%s\n\n", Se, R);
    printf("  \033[1mn%s%s new%s  \033[1mr%s%s rename%s  \033[1md%s%s trash%s  \033[1m?%s%s help%s\n",
           R, H, R, R, H, R, R, H, R, R, H, R);
}

static void render_list(const char *current, size_t sel) {
#define BOX_W 62
    // Redraw in place: home the cursor and clear everything below it
    printf("\033[H\033[J\033[?25l");
    printf("Current theme: \033[1m%s\033[0m  \033[2m(%zu themes, %s colour)\033[0m\n\n",
           current, (size_t)THEME_COUNT, g_true_color ? "24-bit" : "256");

    // Top border: ╔═══ blob theme selector ══...══╗
    printf("╔═══ blob theme selector ");
    for (int i = 0; i < BOX_W - 26; i++) printf("═");
    printf("╗\n");

    size_t start = 0;
    if (THEME_COUNT > VISIBLE_ROWS && sel >= VISIBLE_ROWS / 2) {
        start = sel - VISIBLE_ROWS / 2;
        if (start + VISIBLE_ROWS > THEME_COUNT) start = THEME_COUNT - VISIBLE_ROWS;
    }
    size_t end = start + VISIBLE_ROWS < THEME_COUNT ? start + VISIBLE_ROWS : THEME_COUNT;

    if (start > 0) printf("║  \033[2m%-*s\033[0m║\n", BOX_W - 4 + 2, "\xe2\x86\x91 more");
    else printf("║%*s║\n", BOX_W - 2, "");

    for (size_t i = start; i < end; i++) {
        if (i == sel) {
            char buf[32];
            printf("║  \033[1m%s→ %-18s\033[0m  \033[2m%-34s\033[0m  ║\n",
                   sgr(themes[i].selected, buf, sizeof(buf)), themes[i].name, themes[i].description);
        } else {
            printf("║    %-18s  \033[2m%-34s\033[0m  ║\n", themes[i].name, themes[i].description);
        }
    }

    if (end < THEME_COUNT) printf("║  \033[2m%-*s\033[0m║\n", BOX_W - 4 + 2, "\xe2\x86\x93 more");
    else printf("║%*s║\n", BOX_W - 2, "");

    // Bottom border
    printf("╚");
    for (int i = 0; i < BOX_W - 2; i++) printf("═");
    printf("╝\n");

    render_preview(sel);

    printf("\n\033[2m↑/↓ or k/j preview  ENTER apply  ESC keep %s\033[0m\n", current);
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

    // Read current theme and colour mode from config
    char current_theme[32] = "default";
    char color_mode[16] = "auto";
    FILE *cf = fopen(config_path, "r");
    if (cf) {
        char line[LINE_MAX];
        while (fgets(line, sizeof(line), cf)) {
            char *eq = strchr(line, '=');
            if (!eq) continue;
            char *val = eq + 1;
            while (*val && isspace((unsigned char)*val)) val++;
            size_t vlen = strlen(val);
            while (vlen > 0 && isspace((unsigned char)val[vlen - 1])) val[--vlen] = '\0';
            if (!*val) continue;
            if (strncmp(line, "theme", 5) == 0 && (line[5] == ' ' || line[5] == '=')) {
                snprintf(current_theme, sizeof(current_theme), "%s", resolve_theme_name(val));
            } else if (strncmp(line, "colors", 6) == 0 && (line[6] == ' ' || line[6] == '=')) {
                snprintf(color_mode, sizeof(color_mode), "%s", val);
            }
        }
        fclose(cf);
    }
    for (size_t i = 0; i < THEME_COUNT; i++) {
        if (strcmp(themes[i].name, current_theme) == 0) {
            sel = i;
            break;
        }
    }
    g_true_color = strcmp(color_mode, "truecolor") == 0 ||
                   (strcmp(color_mode, "256") != 0 && terminal_supports_true_color());

    atexit(disable_raw_mode);
#ifndef _WIN32
    setup_signal_handlers();
#endif

    enable_raw_mode();

    bool running = true;
    while (running) {
        render_list(current_theme, sel);
        int key = read_key();

        switch (key) {
        case -1: // up
        case 'k':
            if (sel > 0) sel--;
            break;
        case -2: // down
        case 'j':
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
                snprintf(new_line, sizeof(new_line), "theme = %s\n", themes[sel].name);
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
        snprintf(new_line, sizeof(new_line), "theme = %s\n", themes[sel].name);
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
    printf("Theme set to \033[1m%s\033[0m.\n", themes[sel].name);
    return 0;
}
