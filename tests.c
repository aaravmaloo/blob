#define BLOB_TEST
#include "main.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>

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
    mark_plugin_keybind_conflicts(&list);

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

    printf("\nAll tests passed!\n");
    return 0;
}
