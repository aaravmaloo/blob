/* export.c — blob plugin
 * Export the selected note to HTML or PDF using pandoc.
 * Output file is saved next to the original note.
 * Usage: export <note_path>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define PATH_SEP "\\"
#else
#define PATH_SEP "/"
#endif

/* check whether a command exists on PATH */
static int command_exists(const char *name) {
#ifdef _WIN32
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "where \"%s\" >nul 2>&1", name);
#else
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "command -v \"%s\" >/dev/null 2>&1", name);
#endif
    return system(cmd) == 0;
}

/* build output path: same dir as note, stem + new_ext */
static void make_output_path(const char *note_path, const char *new_ext,
                              char *out, size_t out_size) {
    /* copy note path */
    snprintf(out, out_size, "%s", note_path);

    /* strip .md if present */
    size_t len = strlen(out);
    if (len > 3 && strcmp(out + len - 3, ".md") == 0) {
        out[len - 3] = '\0';
    }

    /* append new extension */
    strncat(out, new_ext, out_size - strlen(out) - 1);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <note_path>\n", argv[0]);
        return 1;
    }

    const char *note_path = argv[1];

    /* ── pandoc check ─────────────────────────────── */
    printf("\n");
    if (!command_exists("pandoc")) {
        printf("  \033[31mError:\033[0m pandoc is not installed or not on PATH.\n");
        printf("  Install pandoc from https://pandoc.org/installing.html\n\n");
        printf("  Press Enter to return...");
        fflush(stdout);
        int ch;
        while ((ch = getchar()) != EOF && ch != '\n' && ch != '\r');
        return 1;
    }

    /* ── format selection ─────────────────────────── */
    printf("  \033[1mExport note\033[0m\n");
    printf("  \033[2m%s\033[0m\n\n", note_path);
    printf("  Choose format:\n");
    printf("    \033[36m[1]\033[0m HTML  (no extra dependencies)\n");
    printf("    \033[36m[2]\033[0m PDF   (requires a LaTeX distribution)\n");
    printf("    \033[36m[3]\033[0m DOCX  (Microsoft Word)\n");
    printf("\n  > ");
    fflush(stdout);

    char choice[8] = "1";
    if (!fgets(choice, sizeof(choice), stdin)) {
        return 1;
    }

    const char *format;
    const char *ext;
    const char *extra_flags = "";

    switch (choice[0]) {
    case '2':
        format      = "pdf";
        ext         = ".pdf";
        extra_flags = "--pdf-engine=xelatex";
        break;
    case '3':
        format      = "docx";
        ext         = ".docx";
        break;
    default:
        format      = "html";
        ext         = ".html";
        extra_flags = "--standalone --self-contained";
        break;
    }

    char out_path[4096];
    make_output_path(note_path, ext, out_path, sizeof(out_path));

    /* ── run pandoc ───────────────────────────────── */
    printf("\n  Exporting to \033[36m%s\033[0m...\n", out_path);
    fflush(stdout);

    char cmd[8192];
    snprintf(cmd, sizeof(cmd),
             "pandoc \"%s\" -f markdown -t %s %s -o \"%s\"",
             note_path, format, extra_flags, out_path);

    int ret = system(cmd);

    if (ret != 0) {
        printf("\n  \033[31mExport failed.\033[0m\n");
        if (choice[0] == '2') {
            printf("  PDF export requires a LaTeX distribution (e.g., MiKTeX, TeX Live).\n");
        }
    } else {
        printf("  \033[32mDone!\033[0m Saved to:\n");
        printf("  %s\n", out_path);

        /* offer to open it */
        printf("\n  Open the exported file? (y/N) ");
        fflush(stdout);
        char open_choice[4];
        if (fgets(open_choice, sizeof(open_choice), stdin) &&
            (open_choice[0] == 'y' || open_choice[0] == 'Y')) {
#ifdef _WIN32
            char open_cmd[4096 + 16];
            snprintf(open_cmd, sizeof(open_cmd), "start \"\" \"%s\"", out_path);
            system(open_cmd);
#elif defined(__APPLE__)
            char open_cmd[4096 + 8];
            snprintf(open_cmd, sizeof(open_cmd), "open \"%s\"", out_path);
            system(open_cmd);
#else
            char open_cmd[4096 + 16];
            snprintf(open_cmd, sizeof(open_cmd), "xdg-open \"%s\" &", out_path);
            system(open_cmd);
#endif
        }
    }

    printf("\n  Press Enter to return...");
    fflush(stdout);
    int ch;
    while ((ch = getchar()) != EOF && ch != '\n' && ch != '\r');
    return ret == 0 ? 0 : 1;
}
