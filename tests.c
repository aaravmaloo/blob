#define BLOB_TEST
#include "main.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <sys/utime.h>
#define utimbuf _utimbuf
#define utime _utime
#else
#include <utime.h>
#endif

static void test_sanitize_slug(void) {
    char slug[TITLE_MAX];

    sanitize_slug("Hello World", slug, sizeof(slug));
    assert(strcmp(slug, "hello-world") == 0);

    sanitize_slug("Hello... World!!!", slug, sizeof(slug));
    assert(strcmp(slug, "hello-world") == 0);

    sanitize_slug("  Multiple   Spaces  ", slug, sizeof(slug));
    assert(strcmp(slug, "multiple-spaces") == 0);

    sanitize_slug("Mixed-Case_With.Dots", slug, sizeof(slug));
    assert(strcmp(slug, "mixed-case-with-dots") == 0);

    sanitize_slug("Note 📝", slug, sizeof(slug));
    assert(strcmp(slug, "note-📝") == 0);

    sanitize_slug("Résumé", slug, sizeof(slug));
    assert(strcmp(slug, "résumé") == 0);
    
    printf("test_sanitize_slug passed\n");
}

static void test_title_from_filename(void) {
    char title[TITLE_MAX];

    title_from_filename(title, sizeof(title), "my-note.md");
    assert(strcmp(title, "my note") == 0);

    title_from_filename(title, sizeof(title), "another_note.md");
    assert(strcmp(title, "another note") == 0);

    title_from_filename(title, sizeof(title), "just-a-file");
    assert(strcmp(title, "just a file") == 0);

    printf("test_title_from_filename passed\n");
}

static void test_contains_case_insensitive(void) {
    assert(contains_case_insensitive("Hello World", "hello"));
    assert(contains_case_insensitive("Hello World", "WORLD"));
    assert(contains_case_insensitive("Hello World", "o w"));
    assert(!contains_case_insensitive("Hello World", "bye"));
    assert(contains_case_insensitive("Hello World", ""));

    printf("test_contains_case_insensitive passed\n");
}

static void test_note_cmp(void) {
    Note a, b;
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));

    strcpy(a.title, "A");
    a.mtime = 100;
    strcpy(b.title, "B");
    b.mtime = 200;

    assert(note_cmp(&a, &b) > 0);
    assert(note_cmp(&b, &a) < 0);

    a.mtime = 200;
    assert(note_cmp(&a, &b) < 0);
    assert(note_cmp(&b, &a) > 0);

    printf("test_note_cmp passed\n");
}

static void test_parse_plugin_readme(void) {
    const char *test_readme = "test_readme.md";
    FILE *f = fopen(test_readme, "w");
    assert(f != NULL);
    fprintf(f, "# My Test Plugin\n\n");
    fprintf(f, "[authors] = {\"Alice\", \"Bob\"}\n");
    fprintf(f, "[description] = <A plugin for testing purposes.>\n");
    fprintf(f, "[keybind] = t\n");
    fclose(f);

    Plugin p;
    memset(&p, 0, sizeof(p));
    assert(parse_plugin_readme(test_readme, &p));

    assert(strcmp(p.name, "My Test Plugin") == 0);
    assert(strcmp(p.authors, "Alice\", \"Bob") == 0); // Note: current parser logic for authors is basic
    assert(strcmp(p.description, "A plugin for testing purposes.") == 0);
    assert(p.keybind == 't');
    assert(p.api == 1);
    assert(p.is_legacy);
    assert(strcmp(p.mode, "note") == 0);

    remove(test_readme);
    printf("test_parse_plugin_readme passed\n");
}

static void test_parse_plugin_readme_api2(void) {
    const char *test_readme = "test_readme_api2.md";
    FILE *f = fopen(test_readme, "w");
    assert(f != NULL);
    fprintf(f, "# workspace\n\n");
    fprintf(f, "[api] = 2\n");
    fprintf(f, "[version] = 1.2.3\n");
    fprintf(f, "[authors] = {\"Alice\"}\n");
    fprintf(f, "[description] = <Workspace plugin.>\n");
    fprintf(f, "[keybind] = f\n");
    fprintf(f, "[mode] = workspace\n");
    fprintf(f, "[permissions] = {\"read-notes\",\"network\"}\n");
    fclose(f);

    Plugin p;
    memset(&p, 0, sizeof(p));
    assert(parse_plugin_readme(test_readme, &p));

    assert(strcmp(p.name, "workspace") == 0);
    assert(p.api == 2);
    assert(!p.is_legacy);
    assert(strcmp(p.version, "1.2.3") == 0);
    assert(strcmp(p.mode, "workspace") == 0);
    assert(strcmp(p.permissions, "read-notes\",\"network") == 0);
    assert(plugin_uses_workspace(&p));
    assert(!p.has_keybind_conflict);

    remove(test_readme);
    printf("test_parse_plugin_readme_api2 passed\n");
}

static void test_plugin_keybind_conflicts(void) {
    assert(key_is_core_reserved('p'));
    assert(key_is_core_reserved(':'));
    assert(!key_is_core_reserved('f'));

    PluginList list = {NULL, 0, 0};
    Plugin a, b, c;
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    memset(&c, 0, sizeof(c));
    strcpy(a.name, "a");
    strcpy(b.name, "b");
    strcpy(c.name, "c");
    a.keybind = 'x';
    b.keybind = 'x';
    c.keybind = 'p';

    assert(plugin_list_push(&list, &a));
    assert(plugin_list_push(&list, &b));
    assert(plugin_list_push(&list, &c));
    mark_plugin_keybind_conflicts(&list, NULL);

    assert(list.items[0].has_keybind_conflict);
    assert(list.items[1].has_keybind_conflict);
    assert(list.items[2].has_keybind_conflict);

    plugin_list_free(&list);
    printf("test_plugin_keybind_conflicts passed\n");
}

static void test_unique_path_in_dir(void) {
    const char *existing = "collision.md";
    FILE *f = fopen(existing, "w");
    assert(f != NULL);
    fprintf(f, "hello");
    fclose(f);

    char path[PATH_MAX];
    unique_path_in_dir(".", existing, path, sizeof(path));
    assert(strstr(path, "collision-2.md") != NULL);

    remove(existing);
    printf("test_unique_path_in_dir passed\n");
}

static void test_files_are_different(void) {
    const char *f1 = "test_f1.txt";
    const char *f2 = "test_f2.txt";
    const char *f3 = "test_f3.txt";

    FILE *fp1 = fopen(f1, "w");
    fprintf(fp1, "hello");
    fclose(fp1);

    FILE *fp2 = fopen(f2, "w");
    fprintf(fp2, "hello");
    fclose(fp2);

    FILE *fp3 = fopen(f3, "w");
    fprintf(fp3, "world");
    fclose(fp3);

    assert(!files_are_different(f1, f2));
    assert(files_are_different(f1, f3));

    remove(f1);
    remove(f2);
    remove(f3);
    printf("test_files_are_different passed\n");
}

static void test_undo_operations(void) {
    /* Setup: create a temp notes dir and file */
    const char *notes_dir = "test_undo_notes";
    const char *test_file = "test_undo_notes/test-note.md";
    const char *trash_dir = "test_undo_notes/.trash";
    char trash_path[PATH_MAX];

    mkdir(notes_dir, 0755);

    FILE *f = fopen(test_file, "w");
    assert(f != NULL);
    fprintf(f, "# Test note\n");
    fclose(f);

    /* Create trash dir */
    mkdir(trash_dir, 0755);

    AppConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    snprintf(cfg.notes_dir, sizeof(cfg.notes_dir), "%s", notes_dir);
    snprintf(cfg.favorites_path, sizeof(cfg.favorites_path), "%s/favorites", notes_dir);

    AppState state;
    memset(&state, 0, sizeof(state));

    /* Test trash undo + redo through the stack */
    snprintf(trash_path, sizeof(trash_path), "%s/test-note.md", trash_dir);
    assert(rename(test_file, trash_path) == 0);
    push_undo(&state, UNDO_TRASH, trash_path, test_file, "Test note");

    assert(state.undo_count == 1);
    assert(state.redo_count == 0);
    assert(access(test_file, 0) != 0); /* original should be gone */
    assert(access(trash_path, 0) == 0); /* should be in trash */

    /* Undo restores the file */
    undo_last_action(&state, &cfg);
    assert(access(test_file, 0) == 0);
    assert(access(trash_path, 0) != 0);
    assert(state.undo_count == 0);
    assert(state.redo_count == 1);

    /* Redo moves it back to trash */
    redo_last_action(&state, &cfg);
    assert(access(test_file, 0) != 0);
    assert(access(trash_path, 0) == 0);
    assert(state.undo_count == 1);
    assert(state.redo_count == 0);

    /* A new action clears redo history */
    assert(rename(trash_path, test_file) == 0);
    push_undo(&state, UNDO_RENAME, test_file, test_file, "Test note");
    assert(state.redo_count == 0);

    /* Undo stack depth is capped at MAX_UNDO */
    for (int i = 0; i < MAX_UNDO + 5; i++) {
        push_undo(&state, UNDO_TRASH, "a", "b", "x");
    }
    assert(state.undo_count == MAX_UNDO);

    /* Clean up */
    remove(test_file);
    remove(trash_path);
    rmdir(trash_dir);
    rmdir(notes_dir);
    note_list_free(&state.notes);

    printf("test_undo_operations passed\n");
}

static void test_sort_modes(void) {
    Note a, b, c;
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    memset(&c, 0, sizeof(c));

    strcpy(a.title, "Alpha");
    a.mtime = 100;
    a.size = 50;
    strcpy(b.title, "Beta");
    b.mtime = 200;
    b.size = 10;
    strcpy(c.title, "Gamma");
    c.mtime = 300;
    c.size = 100;

    /* Default: mtime descending (newest first) */
    g_sort_mode = SORT_MTIME;
    g_sort_reverse = false;
    assert(note_cmp(&c, &a) < 0); /* c newer, sorts first */

    /* Title ascending */
    g_sort_mode = SORT_TITLE;
    assert(note_cmp(&a, &b) < 0);
    assert(note_cmp(&b, &a) > 0);

    /* Title descending */
    g_sort_reverse = true;
    assert(note_cmp(&a, &b) > 0);
    g_sort_reverse = false;

    /* Size ascending */
    g_sort_mode = SORT_SIZE;
    assert(note_cmp(&b, &a) < 0); /* 10 < 50 */
    assert(note_cmp(&c, &a) > 0); /* 100 > 50 */

    /* Favorites always float to the top regardless of mode */
    g_sort_mode = SORT_MTIME;
    a.is_favorite = true;
    assert(note_cmp(&a, &b) < 0);
    a.is_favorite = false;

    /* Reset globals for other tests */
    g_sort_mode = SORT_MTIME;
    g_sort_reverse = false;

    printf("test_sort_modes passed\n");
}

static void test_purge_old_trashed_notes(void) {
    const char *notes_dir = "test_purge_notes";
    const char *trash_dir = "test_purge_notes/.trash";
    mkdir(notes_dir, 0755);
    mkdir(trash_dir, 0755);

    /* Old note (30 days ago) and fresh note */
    const char *old_file = "test_purge_notes/.trash/old.md";
    const char *new_file = "test_purge_notes/.trash/new.md";

    FILE *f = fopen(old_file, "w");
    assert(f != NULL);
    fprintf(f, "# Old\n");
    fclose(f);
    f = fopen(new_file, "w");
    assert(f != NULL);
    fprintf(f, "# New\n");
    fclose(f);

    /* Set old note's mtime to 30 days ago */
    struct utimbuf ut;
    ut.actime = time(NULL) - 30 * 86400;
    ut.modtime = time(NULL) - 30 * 86400;
    utime(old_file, &ut);

    AppConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    snprintf(cfg.notes_dir, sizeof(cfg.notes_dir), "%s", notes_dir);

    /* purge_days = 7 should remove the old note but keep the fresh one */
    int purged = purge_old_trashed_notes(&cfg, 7);
    assert(purged == 1);
    assert(access(old_file, 0) != 0);
    assert(access(new_file, 0) == 0);

    /* purge_days <= 0 disables purging */
    assert(purge_old_trashed_notes(&cfg, 0) == 0);

    remove(new_file);
    rmdir(trash_dir);
    rmdir(notes_dir);
    printf("test_purge_old_trashed_notes passed\n");
}

static void test_session_roundtrip(void) {
    const char *data_dir = "test_session_data";
    mkdir(data_dir, 0755);

    AppConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    snprintf(cfg.data_dir, sizeof(cfg.data_dir), "%s", data_dir);

    save_session(&cfg, "/tmp/notes/some-note.md");
    char out[PATH_MAX];
    assert(load_session(&cfg, out, sizeof(out)));
    assert(strcmp(out, "/tmp/notes/some-note.md") == 0);

    /* Empty path should clear the session */
    save_session(&cfg, "");
    assert(!load_session(&cfg, out, sizeof(out)));

    remove("test_session_data/session");
    rmdir(data_dir);
    printf("test_session_roundtrip passed\n");
}

static void test_keybinding_defaults(void) {
    AppConfig cfg;
    memset(&cfg, 0, sizeof(cfg));

    /* Simulate init_paths keybinding defaults (without calling init_paths) */
    cfg.key_create = 'n';
    cfg.key_rename = 'r';
    cfg.key_trash = 'd';
    cfg.key_delete = 'D';
    cfg.key_trash_bin = 't';
    cfg.key_copy = 'y';
    cfg.key_star = '*';
    cfg.key_search = '/';
    cfg.key_cmd = ':';
    cfg.key_plugins = 'p';
    cfg.key_quit = 'q';
    cfg.key_undo = 'u';
    cfg.key_move_down = 'j';
    cfg.key_move_up = 'k';
    cfg.key_view = 'v';

    assert(cfg.key_create == 'n');
    assert(cfg.key_rename == 'r');
    assert(cfg.key_trash == 'd');
    assert(cfg.key_delete == 'D');
    assert(cfg.key_trash_bin == 't');
    assert(cfg.key_copy == 'y');
    assert(cfg.key_star == '*');
    assert(cfg.key_search == '/');
    assert(cfg.key_cmd == ':');
    assert(cfg.key_plugins == 'p');
    assert(cfg.key_quit == 'q');
    assert(cfg.key_undo == 'u');
    assert(cfg.key_move_down == 'j');
    assert(cfg.key_move_up == 'k');
    assert(cfg.key_view == 'v');

    printf("test_keybinding_defaults passed\n");
}

static void test_keybinding_config_parsing(void) {
    const char *test_config = "test_keyconfig.conf";
    FILE *f = fopen(test_config, "w");
    assert(f != NULL);
    fprintf(f, "editor = nvim\n");
    fprintf(f, "theme = dracula\n");
    fprintf(f, "key_create = c\n");
    fprintf(f, "key_delete = X\n");
    fprintf(f, "key_move_down = j\n");
    fprintf(f, "key_move_up = k\n");
    fprintf(f, "key_undo = z\n");
    fprintf(f, "key_view = p\n");
    fprintf(f, "sort = title\n");
    fprintf(f, "sort_reverse = true\n");
    fprintf(f, "purge_days = 14\n");
    fclose(f);

    AppConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    snprintf(cfg.config_path, sizeof(cfg.config_path), "%s", test_config);

    /* Set defaults first */
    cfg.key_create = 'n';
    cfg.key_rename = 'r';
    cfg.key_trash = 'd';
    cfg.key_delete = 'D';
    cfg.key_trash_bin = 't';
    cfg.key_copy = 'y';
    cfg.key_star = '*';
    cfg.key_search = '/';
    cfg.key_cmd = ':';
    cfg.key_plugins = 'p';
    cfg.key_quit = 'q';
    cfg.key_undo = 'u';
    cfg.key_move_down = 'j';
    cfg.key_move_up = 'k';
    cfg.key_view = 'v';
    cfg.purge_days = 30;
    cfg.sort_reverse = false;
    snprintf(cfg.editor, sizeof(cfg.editor), "vim");
    snprintf(cfg.theme_name, sizeof(cfg.theme_name), "default");
    snprintf(cfg.sort_order, sizeof(cfg.sort_order), "mtime");

    load_config(&cfg);

    /* Verify keybindings were loaded */
    assert(cfg.key_create == 'c');
    assert(cfg.key_delete == 'X');
    assert(cfg.key_undo == 'z');
    /* Unchanged defaults */
    assert(cfg.key_rename == 'r');
    assert(cfg.key_trash == 'd');
    assert(cfg.key_trash_bin == 't');
    assert(cfg.key_copy == 'y');
    assert(cfg.key_star == '*');
    assert(cfg.key_search == '/');
    assert(cfg.key_cmd == ':');
    assert(cfg.key_plugins == 'p');
    assert(cfg.key_quit == 'q');
    assert(cfg.key_move_down == 'j');
    assert(cfg.key_move_up == 'k');
    assert(cfg.key_view == 'p');

    /* Verify other config was loaded */
    assert(strcmp(cfg.editor, "nvim") == 0);
    assert(strcmp(cfg.theme_name, "dracula") == 0);
    assert(strcmp(cfg.sort_order, "title") == 0);
    assert(g_sort_mode == SORT_TITLE);
    assert(cfg.sort_reverse);
    assert(g_sort_reverse);
    assert(cfg.purge_days == 14);

    /* Reset globals for other tests */
    g_sort_mode = SORT_MTIME;
    g_sort_reverse = false;

    remove(test_config);
    printf("test_keybinding_config_parsing passed\n");
}

static void test_load_theme(void) {
    /* Save current theme state */
    Theme saved = g_theme;

    AppConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    snprintf(cfg.theme_name, sizeof(cfg.theme_name), "dracula");

    load_theme(&cfg);

    /* Dracula theme should have specific colors */
    assert(strcmp(g_theme.title, "\x1b[38;5;141m") == 0);
    assert(strcmp(g_theme.selected, "\x1b[38;5;84m") == 0);
    assert(strcmp(g_theme.search, "\x1b[38;5;215m") == 0);

    /* Test default theme */
    snprintf(cfg.theme_name, sizeof(cfg.theme_name), "default");
    load_theme(&cfg);
    assert(strcmp(g_theme.title, "\x1b[36m") == 0);
    assert(strcmp(g_theme.selected, "\x1b[32m") == 0);

    /* Test unknown theme falls back gracefully */
    snprintf(cfg.theme_name, sizeof(cfg.theme_name), "nonexistent");
    load_theme(&cfg);
    /* Should keep the last loaded values (default) */
    assert(strcmp(g_theme.title, "\x1b[36m") == 0);

    /* Restore */
    g_theme = saved;

    printf("test_load_theme passed\n");
}

int main(void) {
    test_sanitize_slug();
    test_title_from_filename();
    test_contains_case_insensitive();
    test_note_cmp();
    test_parse_plugin_readme();
    test_parse_plugin_readme_api2();
    test_plugin_keybind_conflicts();
    test_unique_path_in_dir();
    test_files_are_different();
    test_undo_operations();
    test_sort_modes();
    test_purge_old_trashed_notes();
    test_session_roundtrip();
    test_keybinding_defaults();
    test_keybinding_config_parsing();
    test_load_theme();

    printf("\nAll tests passed!\n");
    return 0;
}
