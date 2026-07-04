/* remind.c — blob plugin
 * Set a timed reminder tied to the selected note.
 * Input examples: "30" or "30m" = 30 min, "1h" = 1 hr, "2h30m" = 2.5 hr
 *
 * Scheduling backend:
 *   Windows : PowerShell MessageBox via Start-Process (no extra tools needed)
 *   macOS   : osascript display notification
 *   Linux   : notify-send (libnotify) — falls back to wall/echo if missing
 *
 * Usage: remind <note_path>
 */

#define _DEFAULT_SOURCE
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

/* ── helpers ────────────────────────────────────────── */

static void get_note_title(const char *path, char *title, size_t sz) {
    const char *base = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }
    snprintf(title, sz, "%s", base);
    size_t len = strlen(title);
    if (len > 3 && strcmp(title + len - 3, ".md") == 0) title[len - 3] = '\0';

    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') {
            const char *p = line + 1;
            while (*p == '#') p++;
            while (*p == ' ') p++;
            size_t l = strlen(p);
            while (l > 0 && (p[l-1] == '\n' || p[l-1] == '\r')) l--;
            if (l > 0) snprintf(title, sz, "%.*s", (int)l, p);
            break;
        }
    }
    fclose(f);
}

/* Parse strings like "30", "30m", "1h", "2h30m", "90m".
 * Returns total seconds, or -1 on error. */
static long parse_delay(const char *s) {
    long hours = 0, minutes = 0;
    const char *p = s;

    while (*p && isspace((unsigned char)*p)) p++;
    if (!*p) return -1;

    /* read leading number */
    long n = 0;
    if (!isdigit((unsigned char)*p)) return -1;
    while (isdigit((unsigned char)*p)) n = n * 10 + (*p++ - '0');

    if (*p == 'h' || *p == 'H') {
        hours = n;
        p++;
        /* optional minutes after 'h' */
        n = 0;
        while (isdigit((unsigned char)*p)) n = n * 10 + (*p++ - '0');
        if (*p == 'm' || *p == 'M') { minutes = n; p++; }
    } else if (*p == 'm' || *p == 'M' || *p == '\0' || isspace((unsigned char)*p)) {
        minutes = n;
        if (*p) p++;
    } else {
        return -1;
    }

    while (*p && isspace((unsigned char)*p)) p++;
    if (*p) return -1;  /* trailing garbage */

    long total = hours * 3600L + minutes * 60L;
    return total > 0 ? total : -1;
}

static void format_delay(long seconds, char *buf, size_t sz) {
    long h = seconds / 3600;
    long m = (seconds % 3600) / 60;
    if (h > 0 && m > 0)      snprintf(buf, sz, "%ldh %ldm", h, m);
    else if (h > 0)           snprintf(buf, sz, "%ldh", h);
    else                      snprintf(buf, sz, "%ldm", m);
}

/* ── platform scheduling ─────────────────────────── */

#ifdef _WIN32

static int schedule_windows(long delay_sec, const char *title) {
    /* Build a PowerShell one-liner that:
     *   1. waits delay_sec seconds (Start-Sleep)
     *   2. pops a MessageBox via WinForms
     * Run it hidden and detached so it survives after this process exits. */
    char ps[2048];
    /* escape single quotes in title */
    char safe_title[512];
    size_t j = 0;
    for (size_t i = 0; title[i] && j + 2 < sizeof(safe_title); i++) {
        if (title[i] == '\'') safe_title[j++] = '\''; /* double it */
        safe_title[j++] = title[i];
    }
    safe_title[j] = '\0';

    snprintf(ps, sizeof(ps),
        "Start-Sleep -Seconds %ld; "
        "Add-Type -AssemblyName System.Windows.Forms; "
        "[System.Windows.Forms.MessageBox]::Show("
            "'blob reminder: %s',"
            "'blob',"
            "[System.Windows.Forms.MessageBoxButtons]::OK,"
            "[System.Windows.Forms.MessageBoxIcon]::Information)",
        delay_sec, safe_title);

    char cmd[2300];
    snprintf(cmd, sizeof(cmd),
             "powershell -WindowStyle Hidden -NonInteractive -Command \"%s\"",
             ps);

    /* Use CreateProcess so we can set CREATE_NO_WINDOW + detach */
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);

    BOOL ok = CreateProcessA(
        NULL, cmd,
        NULL, NULL, FALSE,
        CREATE_NO_WINDOW | DETACHED_PROCESS,
        NULL, NULL, &si, &pi);

    if (ok) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    return ok ? 0 : 1;
}

#else /* POSIX */

static int command_exists_posix(const char *name) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "command -v '%s' >/dev/null 2>&1", name);
    return system(cmd) == 0;
}

static int schedule_posix(long delay_sec, const char *title) {
    /* Escape double quotes and backslashes for shell embedding */
    char safe[512];
    size_t j = 0;
    for (size_t i = 0; title[i] && j + 3 < sizeof(safe); i++) {
        if (title[i] == '"' || title[i] == '\\' || title[i] == '\'') {
            safe[j++] = '\\';
        }
        safe[j++] = title[i];
    }
    safe[j] = '\0';

    char script[2048];

#if defined(__APPLE__)
    /* macOS — osascript notification */
    snprintf(script, sizeof(script),
             "sleep %ld && osascript -e "
             "'display notification \"%s\" with title \"blob reminder\"' "
             ">/dev/null 2>&1",
             delay_sec, safe);
#else
    /* Linux — prefer notify-send, fall back to wall */
    if (command_exists_posix("notify-send")) {
        snprintf(script, sizeof(script),
                 "sleep %ld && notify-send 'blob reminder' '%s' >/dev/null 2>&1",
                 delay_sec, safe);
    } else {
        snprintf(script, sizeof(script),
                 "sleep %ld && echo 'blob reminder: %s' | wall 2>/dev/null",
                 delay_sec, safe);
    }
#endif

    /* Wrap in nohup + bash so it survives this process exiting */
    char full_cmd[2200];
    snprintf(full_cmd, sizeof(full_cmd),
             "nohup bash -c '%s' >/dev/null 2>&1 &",
             script);

    return system(full_cmd) == 0 ? 0 : 1;
}

#endif /* POSIX */

/* ── main ───────────────────────────────────────────── */

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <note_path>\n", argv[0]);
        return 1;
    }

    char title[256];
    get_note_title(argv[1], title, sizeof(title));

    printf("\n");
    printf("  \033[1mSet reminder\033[0m\n");
    printf("  Note: \033[36m%s\033[0m\n\n", title);
    printf("  Enter delay (e.g.  30  30m  1h  2h30m):\n");
    printf("  > ");
    fflush(stdout);

    char input[64];
    if (!fgets(input, sizeof(input), stdin)) return 1;
    /* strip newline */
    input[strcspn(input, "\r\n")] = '\0';

    if (!input[0]) {
        printf("  Cancelled.\n\n");
        fflush(stdout);
        return 0;
    }

    long delay = parse_delay(input);
    if (delay < 0) {
        printf("\n  \033[31mInvalid time format.\033[0m Use: 30  30m  1h  2h30m\n\n");
        printf("  Press Enter to return...");
        fflush(stdout);
        int ch;
        while ((ch = getchar()) != EOF && ch != '\n' && ch != '\r');
        return 1;
    }

    char delay_str[32];
    format_delay(delay, delay_str, sizeof(delay_str));

    printf("\n  Scheduling reminder in \033[32m%s\033[0m for:\n", delay_str);
    printf("  \"%s\"\n\n", title);
    fflush(stdout);

#ifdef _WIN32
    int ok = schedule_windows(delay, title);
#else
    int ok = schedule_posix(delay, title);
#endif

    if (ok != 0) {
        printf("  \033[31mFailed to schedule reminder.\033[0m\n");
        printf("  On Linux, make sure 'nohup' and 'bash' are available.\n");
    } else {
        printf("  \033[32mReminder set!\033[0m You will be notified in %s.\n", delay_str);
    }

    printf("\n  Press Enter to return...");
    fflush(stdout);
    int ch;
    while ((ch = getchar()) != EOF && ch != '\n' && ch != '\r');
    return ok;
}
