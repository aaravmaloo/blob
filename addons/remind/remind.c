/* remind.c — blob plugin
 * Set a timed reminder tied to the selected note.
 * Input examples: "30" or "30m" = 30 min, "1h" = 1 hr, "2h30m" = 2.5 hr
 *
 * Scheduling backend:
 *   Windows : PowerShell MessageBox via Start-Process (no extra tools needed)
 *   macOS   : osascript display notification
 *   Linux   : notify-send (libnotify) — falls back to wall/echo if missing
 *
 * Persists reminder to $data_dir/reminders.txt so Ctrl+R in blob shows
 * all active reminders with time remaining.
 *
 * Usage: remind <note_path>
 */

#define _DEFAULT_SOURCE
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#define PATH_SEP "\\"
#else
#include <unistd.h>
#define PATH_SEP "/"
#endif

/* ── path helpers ───────────────────────────────────── */

/* Extract the parent directory of a path (modifies a copy). */
static void get_dirname(const char *path, char *out, size_t out_size) {
    snprintf(out, out_size, "%s", path);
    char *last = NULL;
    for (char *p = out; *p; p++) {
        if (*p == '/' || *p == '\\') last = p;
    }
    if (last && last != out) {
        *last = '\0';
    } else if (last == out) {
        /* root-level path */
        out[1] = '\0';
    } else {
        out[0] = '.'; out[1] = '\0';
    }
}

/* Derive the blob data_dir from a note path.
 * note_path  = $data_dir/notes/my-note.md
 * notes_dir  = dirname(note_path)  → $data_dir/notes
 * data_dir   = dirname(notes_dir)  → $data_dir             */
static void get_data_dir(const char *note_path, char *data_dir, size_t sz) {
    char notes_dir[4096];
    get_dirname(note_path, notes_dir, sizeof(notes_dir));
    get_dirname(notes_dir, data_dir, sz);
}

/* ── note title ─────────────────────────────────────── */

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

/* ── delay parser ───────────────────────────────────── */

/* Accepts: "30" "30m" "1h" "2h30m" — returns total seconds or -1 on error */
static long parse_delay(const char *s) {
    long hours = 0, minutes = 0;
    const char *p = s;

    while (*p && isspace((unsigned char)*p)) p++;
    if (!*p || !isdigit((unsigned char)*p)) return -1;

    long n = 0;
    while (isdigit((unsigned char)*p)) n = n * 10 + (*p++ - '0');

    if (*p == 'h' || *p == 'H') {
        hours = n; p++;
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
    if (*p) return -1;

    long total = hours * 3600L + minutes * 60L;
    return total > 0 ? total : -1;
}

static void format_delay(long seconds, char *buf, size_t sz) {
    long h = seconds / 3600;
    long m = (seconds % 3600) / 60;
    if (h > 0 && m > 0)  snprintf(buf, sz, "%ldh %ldm", h, m);
    else if (h > 0)      snprintf(buf, sz, "%ldh", h);
    else                 snprintf(buf, sz, "%ldm", m);
}

/* ── persist reminder ───────────────────────────────── */

/* Append one line to data_dir/reminders.txt:
 *   <ring_at_unix_timestamp>\t<note_title>\n
 * The file is read by blob core (Ctrl+R) to show active reminders. */
static void save_reminder(const char *note_path, long ring_at, const char *title) {
    char data_dir[4096];
    get_data_dir(note_path, data_dir, sizeof(data_dir));

    char rem_path[4096 + 32];
    snprintf(rem_path, sizeof(rem_path), "%s" PATH_SEP "reminders.txt", data_dir);

    FILE *f = fopen(rem_path, "a");
    if (!f) return;
    fprintf(f, "%ld\t%s\n", ring_at, title);
    fclose(f);
}

/* ── platform scheduling ─────────────────────────────── */

#ifdef _WIN32

static int schedule_windows(long delay_sec, const char *title) {
    char safe_title[512];
    size_t j = 0;
    for (size_t i = 0; title[i] && j + 2 < sizeof(safe_title); i++) {
        if (title[i] == '\'') safe_title[j++] = '\'';
        safe_title[j++] = title[i];
    }
    safe_title[j] = '\0';

    char ps[2048];
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

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);

    BOOL ok = CreateProcessA(
        NULL, cmd, NULL, NULL, FALSE,
        CREATE_NO_WINDOW | DETACHED_PROCESS,
        NULL, NULL, &si, &pi);

    if (ok) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    return ok ? 0 : 1;
}

#else

static int command_exists_posix(const char *name) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "command -v '%s' >/dev/null 2>&1", name);
    return system(cmd) == 0;
}

static int schedule_posix(long delay_sec, const char *title) {
    char safe[512];
    size_t j = 0;
    for (size_t i = 0; title[i] && j + 3 < sizeof(safe); i++) {
        if (title[i] == '"' || title[i] == '\\' || title[i] == '\'') safe[j++] = '\\';
        safe[j++] = title[i];
    }
    safe[j] = '\0';

    char script[2048];

#if defined(__APPLE__)
    snprintf(script, sizeof(script),
             "sleep %ld && osascript -e "
             "'display notification \"%s\" with title \"blob reminder\"' "
             ">/dev/null 2>&1",
             delay_sec, safe);
#else
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

    char full_cmd[2200];
    snprintf(full_cmd, sizeof(full_cmd),
             "nohup bash -c '%s' >/dev/null 2>&1 &",
             script);

    return system(full_cmd) == 0 ? 0 : 1;
}

#endif

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
        /* persist so Ctrl+R in blob shows it with time remaining */
        long ring_at = (long)time(NULL) + delay;
        save_reminder(argv[1], ring_at, title);

        printf("  \033[32mReminder set!\033[0m You will be notified in %s.\n", delay_str);
        printf("  \033[2mPress Ctrl+R in blob to view active reminders.\033[0m\n");
    }

    printf("\n  Press Enter to return...");
    fflush(stdout);
    int ch;
    while ((ch = getchar()) != EOF && ch != '\n' && ch != '\r');
    return ok;
}
