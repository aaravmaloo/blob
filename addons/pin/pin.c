#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#else
#include <unistd.h>
#include <libgen.h>
#endif

#define PIN_PREFIX "pin-"
#define PIN_PREFIX_LEN 4

static void trim_newline(char *s) {
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) {
        s[--len] = '\0';
    }
}

static bool is_pinned(const char *filename) {
    return strncmp(filename, PIN_PREFIX, PIN_PREFIX_LEN) == 0;
}

static void add_pin_prefix(char *filename) {
    if (!is_pinned(filename)) {
        char new_name[512];
        snprintf(new_name, sizeof(new_name), "%s%s", PIN_PREFIX, filename);
        strcpy(filename, new_name);
    }
}

static void remove_pin_prefix(char *filename) {
    if (is_pinned(filename)) {
        memmove(filename, filename + PIN_PREFIX_LEN, strlen(filename) - PIN_PREFIX_LEN + 1);
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <note_path>\n", argv[0]);
        return 1;
    }

    char *note_path = argv[1];
    char dir_path[1024];
    char file_name[256];

    char *last_sep = strrchr(note_path, '/');
    if (!last_sep) last_sep = strrchr(note_path, '\\');

    if (last_sep) {
        size_t dir_len = last_sep - note_path;
        strncpy(dir_path, note_path, dir_len);
        dir_path[dir_len] = '\0';
        strcpy(file_name, last_sep + 1);
    } else {
        strcpy(dir_path, ".");
        strcpy(file_name, note_path);
    }

    if (is_pinned(file_name)) {
        char new_name[256];
        strcpy(new_name, file_name);
        remove_pin_prefix(new_name);

        char new_path[1024];
        snprintf(new_path, sizeof(new_path), "%s/%s", dir_path, new_name);

        if (rename(note_path, new_path) == 0) {
            printf("Note unpinned: %s\n", new_name);
        } else {
            perror("Failed to unpin note");
            return 1;
        }
    } else {
        char new_name[256];
        strcpy(new_name, file_name);
        add_pin_prefix(new_name);

        char new_path[1024];
        snprintf(new_path, sizeof(new_path), "%s/%s", dir_path, new_name);

        if (rename(note_path, new_path) == 0) {
            printf("Note pinned: %s\n", new_name);
        } else {
            perror("Failed to pin note");
            return 1;
        }
    }

    printf("Press any key to return...");
    fflush(stdout);
    getchar();

    return 0;
}