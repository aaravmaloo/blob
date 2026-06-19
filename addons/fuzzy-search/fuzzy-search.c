#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#define PATH_SEP "\\"
#else
#include <dirent.h>
#define PATH_SEP "/"
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef struct {
    char path[PATH_MAX];
    char title[256];
    int score;
} Match;

static int fuzzy_score(const char *text, const char *query) {
    int score = 0;
    int streak = 0;
    size_t qi = 0;

    for (size_t i = 0; text[i] && query[qi]; i++) {
        if (tolower((unsigned char)text[i]) == tolower((unsigned char)query[qi])) {
            streak++;
            score += 10 + streak * 3;
            qi++;
        } else {
            streak = 0;
        }
    }

    return query[qi] ? 0 : score;
}

static void title_from_filename(char *dst, size_t dst_size, const char *filename) {
    snprintf(dst, dst_size, "%s", filename);
    size_t len = strlen(dst);
    if (len > 3 && strcmp(dst + len - 3, ".md") == 0) dst[len - 3] = '\0';
    for (char *p = dst; *p; p++) {
        if (*p == '-' || *p == '_') *p = ' ';
    }
}

static int score_file(const char *path, const char *title, const char *query) {
    int best = fuzzy_score(title, query) * 2;
    FILE *f = fopen(path, "r");
    if (!f) return best;

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        int score = fuzzy_score(line, query);
        if (score > best) best = score;
    }

    fclose(f);
    return best;
}

#ifndef _WIN32
static int has_md_extension(const char *name) {
    size_t len = strlen(name);
    return len > 3 && strcmp(name + len - 3, ".md") == 0;
}
#endif

static void push_match(Match *matches, size_t *count, const char *path, const char *title, int score) {
    if (score <= 0) return;

    size_t pos = *count;
    if (pos < 10) {
        (*count)++;
    } else if (score <= matches[9].score) {
        return;
    } else {
        pos = 9;
    }

    while (pos > 0 && matches[pos - 1].score < score) {
        matches[pos] = matches[pos - 1];
        pos--;
    }

    snprintf(matches[pos].path, sizeof(matches[pos].path), "%s", path);
    snprintf(matches[pos].title, sizeof(matches[pos].title), "%s", title);
    matches[pos].score = score;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <notes_dir>\n", argv[0]);
        return 1;
    }

    char query[256];
    printf("Fuzzy search\n> ");
    fflush(stdout);
    if (!fgets(query, sizeof(query), stdin)) return 1;
    query[strcspn(query, "\r\n")] = '\0';
    if (!query[0]) return 0;

    Match matches[10];
    size_t count = 0;

#ifdef _WIN32
    char pattern[PATH_MAX];
    snprintf(pattern, sizeof(pattern), "%s" PATH_SEP "*.md", argv[1]);
    WIN32_FIND_DATAA data;
    HANDLE find = FindFirstFileA(pattern, &data);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            char path[PATH_MAX], title[256];
            snprintf(path, sizeof(path), "%s" PATH_SEP "%s", argv[1], data.cFileName);
            title_from_filename(title, sizeof(title), data.cFileName);
            push_match(matches, &count, path, title, score_file(path, title, query));
        } while (FindNextFileA(find, &data));
        FindClose(find);
    }
#else
    DIR *dir = opendir(argv[1]);
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (!has_md_extension(entry->d_name)) continue;
            char path[PATH_MAX], title[256];
            snprintf(path, sizeof(path), "%s" PATH_SEP "%s", argv[1], entry->d_name);
            title_from_filename(title, sizeof(title), entry->d_name);
            push_match(matches, &count, path, title, score_file(path, title, query));
        }
        closedir(dir);
    }
#endif

    if (count == 0) {
        printf("No matches.\n");
    } else {
        for (size_t i = 0; i < count; i++) {
            printf("%zu. %s\n   %s\n", i + 1, matches[i].title, matches[i].path);
        }
    }

    printf("\nPress Enter to return...");
    fflush(stdout);
    getchar();
    return 0;
}
