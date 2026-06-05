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

    remove(test_readme);
    printf("test_parse_plugin_readme passed\n");
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
    test_files_are_different();

    printf("\nAll tests passed!\n");
    return 0;
}
