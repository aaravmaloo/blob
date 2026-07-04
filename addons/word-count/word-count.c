/* word-count.c — blob plugin
 * Shows word count, character count, line count, sentence count,
 * and estimated reading time for the selected note.
 * Usage: word-count <note_path>
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Extract a display title from the note file:
 * returns the first "# Heading" line, or the filename stem if none. */
static void get_note_title(const char *path, char *title, size_t title_size) {
    /* default: derive from filename */
    const char *base = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }
    snprintf(title, title_size, "%s", base);
    /* strip .md */
    size_t len = strlen(title);
    if (len > 3 && strcmp(title + len - 3, ".md") == 0) {
        title[len - 3] = '\0';
    }

    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') {
            const char *p = line + 1;
            while (*p == '#') p++;       /* skip additional # */
            while (*p == ' ') p++;       /* skip leading space */
            size_t l = strlen(p);
            while (l > 0 && (p[l - 1] == '\n' || p[l - 1] == '\r')) l--;
            if (l > 0) {
                snprintf(title, title_size, "%.*s", (int)l, p);
            }
            break;
        }
    }
    fclose(f);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <note_path>\n", argv[0]);
        return 1;
    }

    const char *path = argv[1];
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "Cannot open: %s\n", path);
        return 1;
    }

    long chars      = 0;  /* all chars excluding newlines */
    long words      = 0;
    long lines      = 0;
    long sentences  = 0;
    long paragraphs = 0;
    int  in_word    = 0;
    int  blank_line = 1;  /* treat start as blank so first non-blank = new para */
    int  c;

    while ((c = fgetc(f)) != EOF) {
        if (c == '\r') continue;      /* skip CR in CRLF */

        if (c == '\n') {
            lines++;
            if (!blank_line) {
                /* the line we just finished was non-blank */
                blank_line = 1;
            } else {
                /* two blanks in a row — paragraph break already counted */
            }
            in_word = 0;
            continue;
        }

        chars++;

        /* paragraph detection: first non-space char after blank line */
        if (!isspace((unsigned char)c) && blank_line) {
            paragraphs++;
            blank_line = 0;
        }

        /* sentence endings */
        if (c == '.' || c == '!' || c == '?') {
            sentences++;
        }

        /* word counting */
        if (isspace((unsigned char)c)) {
            in_word = 0;
        } else if (!in_word) {
            in_word = 1;
            words++;
        }
    }
    fclose(f);

    /* reading time: ~238 wpm average silent reading speed */
    long read_sec  = words > 0 ? (long)(words * 60L / 238) : 0;
    long read_min  = read_sec / 60;
    long read_srem = read_sec % 60;

    char title[256];
    get_note_title(path, title, sizeof(title));

    /* ── render ─────────────────────────────────────── */
    printf("\n");
    printf("  \033[1m%s\033[0m\n", title);
    printf("  \033[2m%s\033[0m\n", path);
    printf("\n");
    printf("  \033[36m%-18s\033[0m %ld\n",  "Words",       words);
    printf("  \033[36m%-18s\033[0m %ld\n",  "Characters",  chars);
    printf("  \033[36m%-18s\033[0m %ld\n",  "Lines",       lines);
    printf("  \033[36m%-18s\033[0m %ld\n",  "Sentences",   sentences);
    printf("  \033[36m%-18s\033[0m %ld\n",  "Paragraphs",  paragraphs);
    printf("\n");

    printf("  \033[36m%-18s\033[0m ", "Reading time");
    if (read_min > 0) {
        printf("%ldm %lds", read_min, read_srem);
    } else if (read_sec > 0) {
        printf("%lds", read_sec);
    } else {
        printf("< 1s");
    }
    printf("\n\n");

    printf("  Press Enter to return...");
    fflush(stdout);
    /* wait for Enter */
    int ch;
    while ((ch = getchar()) != EOF && ch != '\n' && ch != '\r');
    return 0;
}
