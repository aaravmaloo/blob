#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static void trim(char *s) {
    size_t start = 0;
    while (isspace((unsigned char)s[start])) start++;
    if (start) memmove(s, s + start, strlen(s + start) + 1);

    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) s[--len] = '\0';
}

static int starts_with_tags(const char *line) {
    return strncmp(line, "tags:", 5) == 0 || strncmp(line, "Tags:", 5) == 0;
}

static int read_file(const char *path, char **out) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    if (size < 0) {
        fclose(f);
        return 0;
    }
    char *buf = malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return 0;
    }
    size_t n = fread(buf, 1, (size_t)size, f);
    buf[n] = '\0';
    fclose(f);
    *out = buf;
    return 1;
}

static int write_file(const char *path, const char *content) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    fputs(content, f);
    fclose(f);
    return 1;
}

static int tag_exists(const char *tags, const char *tag) {
    char copy[1024];
    snprintf(copy, sizeof(copy), "%s", tags);

    char *part = strtok(copy, ",");
    while (part) {
        trim(part);
        if (strcmp(part, tag) == 0) return 1;
        part = strtok(NULL, ",");
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <note_path>\n", argv[0]);
        return 1;
    }

    char *content = NULL;
    if (!read_file(argv[1], &content)) {
        fprintf(stderr, "Could not read note.\n");
        return 1;
    }

    char tags[1024] = "";
    char *line_end = strchr(content, '\n');
    if (starts_with_tags(content)) {
        size_t len = line_end ? (size_t)(line_end - content) : strlen(content);
        if (len > 5 && len - 5 < sizeof(tags)) {
            memcpy(tags, content + 5, len - 5);
            tags[len - 5] = '\0';
            trim(tags);
        }
    }

    printf("Tags: %s\n", tags[0] ? tags : "(none)");
    printf("[a] add  [r] remove all  [Enter] return\n> ");
    fflush(stdout);

    char choice[16];
    if (!fgets(choice, sizeof(choice), stdin)) {
        free(content);
        return 1;
    }

    if (choice[0] == 'a' || choice[0] == 'A') {
        char tag[128];
        printf("Tag to add\n> ");
        fflush(stdout);
        if (!fgets(tag, sizeof(tag), stdin)) {
            free(content);
            return 1;
        }
        tag[strcspn(tag, "\r\n")] = '\0';
        trim(tag);
        if (!tag[0] || tag_exists(tags, tag)) {
            free(content);
            return 0;
        }

        char new_tags[1200];
        snprintf(new_tags, sizeof(new_tags), "%s%s%s", tags, tags[0] ? ", " : "", tag);

        size_t rest_offset = starts_with_tags(content) && line_end ? (size_t)(line_end - content + 1) : 0;
        size_t new_size = strlen(content + rest_offset) + strlen(new_tags) + 16;
        char *next = malloc(new_size);
        if (!next) {
            free(content);
            return 1;
        }
        snprintf(next, new_size, "tags: %s\n%s", new_tags, content + rest_offset);
        if (!write_file(argv[1], next)) {
            fprintf(stderr, "Could not write note.\n");
            free(next);
            free(content);
            return 1;
        }
        free(next);
        printf("Added tag: %s\n", tag);
    } else if (choice[0] == 'r' || choice[0] == 'R') {
        if (starts_with_tags(content)) {
            const char *rest = line_end ? line_end + 1 : "";
            if (!write_file(argv[1], rest)) {
                fprintf(stderr, "Could not write note.\n");
                free(content);
                return 1;
            }
        }
        printf("Tags removed.\n");
    }

    free(content);
    printf("Press Enter to return...");
    fflush(stdout);
    getchar();
    return 0;
}
