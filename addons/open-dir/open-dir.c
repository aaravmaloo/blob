/* open-dir.c — blob plugin
 * Opens the folder containing the selected note in the system file explorer.
 * Works on Windows (explorer), macOS (open), and Linux (xdg-open).
 * Usage: open-dir <note_path>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <note_path>\n", argv[0]);
        return 1;
    }

    /* ── extract parent directory ───────────────────── */
    char dir[4096];
    snprintf(dir, sizeof(dir), "%s", argv[1]);

    char *last = NULL;
    for (char *p = dir; *p; p++) {
        if (*p == '/' || *p == '\\') last = p;
    }
    if (last) {
        *last = '\0';
    } else {
        /* no separator — use current dir */
        dir[0] = '.';
        dir[1] = '\0';
    }

    printf("\n  Opening folder: %s\n\n", dir);
    fflush(stdout);

    /* ── launch explorer / open / xdg-open ─────────── */
#ifdef _WIN32
    /* ShellExecute is safer than system() on Windows for paths with spaces */
    HINSTANCE result = ShellExecuteA(NULL, "explore", dir, NULL, NULL, SW_SHOWNORMAL);
    int ok = (int)(intptr_t)result > 32;
    if (!ok) {
        fprintf(stderr, "  Failed to open folder (ShellExecute error %d).\n",
                (int)(intptr_t)result);
        return 1;
    }
    return 0;
#elif defined(__APPLE__)
    char cmd[4096 + 8];
    /* use open -R to reveal the note itself (highlights file in Finder) */
    snprintf(cmd, sizeof(cmd), "open -R \"%s\" 2>/dev/null || open \"%s\" 2>/dev/null",
             argv[1], dir);
    return system(cmd) == 0 ? 0 : 1;
#else
    char cmd[4096 + 32];
    snprintf(cmd, sizeof(cmd), "xdg-open \"%s\" 2>/dev/null", dir);
    return system(cmd) == 0 ? 0 : 1;
#endif
}
