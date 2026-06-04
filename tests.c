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
    // Unicode characters should now be preserved
    assert(strcmp(slug, "note-📝") == 0);

    sanitize_slug("Résumé", slug, sizeof(slug));
    assert(strcmp(slug, "résumé") == 0);
    
    printf("test_sanitize_slug passed (including Unicode support)\n");
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

    // Latest first (b > a)
    assert(note_cmp(&a, &b) > 0);
    assert(note_cmp(&b, &a) < 0);

    // Same time, alphabetical
    a.mtime = 200;
    assert(note_cmp(&a, &b) < 0);
    assert(note_cmp(&b, &a) > 0);

    printf("test_note_cmp passed\n");
}

int main(void) {
    test_sanitize_slug();
    test_title_from_filename();
    test_contains_case_insensitive();
    test_note_cmp();

    printf("\nAll tests passed!\n");
    return 0;
}
