/* auto-save.c — blob plugin
 * Periodically backs up the selected note to a .backups directory.
 * Each backup is timestamped so you can recover previous versions.
 * Usage: auto-save <note_path>
 *
 * The plugin creates a .backups folder inside the notes directory
 * and saves timestamped copies of the note each time it runs.
 */

#define _DEFAULT_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define PATH_SEP "\\"
#define mkdir(path, mode) _mkdir(path)
#else
#include <unistd.h>
#define PATH_SEP "/"
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* Extract the directory part of a path */
static void get_dirname(const char *path, char *out, size_t out_size) {
    snprintf(out, out_size, "%s", path);
    char *last = NULL;
    for (char *p = out; *p; p++) {
        if (*p == '/' || *p == '\\') last = p;
    }
    if (last) *last = '\0';
    else { out[0] = '.'; out[1] = '\0'; }
}

/* Get the note title (filename without .md) */
static void get_note_title(const char *path, char *title, size_t sz) {
    const char *base = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }
    snprintf(title, sz, "%s", base);
    size_t len = strlen(title);
    if (len > 3 && strcmp(title + len - 3, ".md") == 0) title[len - 3] = '\0';
}

/* Ensure a directory exists */
static int ensure_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) return 1;

    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            char sep = *p;
            *p = '\0';
            mkdir(tmp, 0755);
            *p = sep;
        }
    }
    return mkdir(tmp, 0755) == 0 || errno == EEXIST;
}

/* Format a timestamp into a filename-safe string */
static void format_timestamp(char *buf, size_t sz) {
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    strftime(buf, sz, "%Y%m%d_%H%M%S", tm);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <note_path>\n", argv[0]);
        return 1;
    }

    const char *note_path = argv[1];

    /* Build the backup directory path */
    char notes_dir[PATH_MAX];
    get_dirname(note_path, notes_dir, sizeof(notes_dir));

    char backup_dir[PATH_MAX];
    snprintf(backup_dir, sizeof(backup_dir), "%s" PATH_SEP ".backups", notes_dir);

    if (!ensure_dir(backup_dir)) {
        fprintf(stderr, "Failed to create backup directory: %s\n", backup_dir);
        return 1;
    }

    /* Get note title for the backup filename */
    char title[256];
    get_note_title(note_path, title, sizeof(title));

    /* Read the current note content */
    FILE *src = fopen(note_path, "rb");
    if (!src) {
        fprintf(stderr, "Could not open note: %s\n", note_path);
        return 1;
    }

    fseek(src, 0, SEEK_END);
    long fsize = ftell(src);
    fseek(src, 0, SEEK_SET);

    char *content = malloc((size_t)fsize + 1);
    if (!content) {
        fclose(src);
        fprintf(stderr, "Memory error.\n");
        return 1;
    }

    size_t bytes_read = fread(content, 1, (size_t)fsize, src);
    content[bytes_read] = '\0';
    fclose(src);

    /* Build backup path with timestamp */
    char timestamp[32];
    format_timestamp(timestamp, sizeof(timestamp));

    char backup_path[PATH_MAX];
    snprintf(backup_path, sizeof(backup_path), "%s" PATH_SEP "%s_%s.md",
             backup_dir, title, timestamp);

    /* Write backup */
    FILE *dst = fopen(backup_path, "wb");
    if (!dst) {
        fprintf(stderr, "Could not create backup: %s\n", backup_path);
        free(content);
        return 1;
    }

    fwrite(content, 1, bytes_read, dst);
    fclose(dst);
    free(content);

    printf("\n");
    printf("  \033[1mAuto-save\033[0m\n\n");
    printf("  \033[32mBackup saved!\033[0m\n");
    printf("  \033[2m%s\033[0m\n\n", backup_path);

    /* Show recent backups for this note */
    printf("  \033[2mRecent backups:\033[0m\n");

#ifdef _WIN32
    char pattern[PATH_MAX];
    snprintf(pattern, sizeof(pattern), "%s" PATH_SEP "%s_*.md", backup_dir, title);
    WIN32_FIND_DATAA data;
    HANDLE find = FindFirstFileA(pattern, &data);
    if (find != INVALID_HANDLE_VALUE) {
        int count = 0;
        do {
            if (count < 5) {
                printf("    \033[90m%s\033[0m\n", data.cFileName);
            }
            count++;
        } while (FindNextFileA(find, &data));
        FindClose(find);
        if (count > 5) {
            printf("    \033[90m... and %d more\033[0m\n", count - 5);
        }
    }
#else
    char cmd[PATH_MAX * 2];
    snprintf(cmd, sizeof(cmd),
             "ls -1t \"%s\" | grep \"^%s_\" | head -5 | sed 's/^/    \\\\033[90m/' | sed 's/$/\\\\033[0m/'",
             backup_dir, title);
    fflush(stdout);
    system(cmd);
#endif

    printf("\n  Press Enter to return...");
    fflush(stdout);
    int ch;
    while ((ch = getchar()) != EOF && ch != '\n' && ch != '\r');

    return 0;
}
