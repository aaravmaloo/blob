#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <note_path>\n", argv[0]);
        return 1;
    }

    FILE *f = fopen(argv[1], "r");
    if (!f) {
        perror("Error opening file");
        return 1;
    }

    long long chars = 0;
    long long words = 0;
    long long lines = 0;
    int in_word = 0;
    int c;

    int last_c = '\n';

    while ((c = fgetc(f)) != EOF) {
        chars++;
        if (c == '\n') lines++;
        
        if (isspace(c)) {
            in_word = 0;
        } else if (!in_word) {
            in_word = 1;
            words++;
        }
        last_c = c;
    }

    if (chars > 0 && last_c != '\n') lines++;

    fclose(f);

    printf("\n--- Note Statistics ---\n");
    printf("Path: %s\n", argv[1]);
    printf("Characters: %lld\n", chars);
    printf("Words:      %lld\n", words);
    printf("Lines:      %lld\n", lines);
    printf("-----------------------\n");
    printf("Press any key to return to blob...");
    
    // Simple way to wait for a keypress in a cross-platform-ish way for a plugin
    getchar();

    return 0;
}
