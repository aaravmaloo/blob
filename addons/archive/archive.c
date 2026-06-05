#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <libgen.h>
#endif

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <note_path>\n", argv[0]);
        return 1;
    }

    char *note_path = argv[1];
    char dir_path[1024];
    char file_name[256];

    // Split path into directory and filename
    char *last_sep = strrchr(note_path, '/');
    if (!last_sep) last_sep = strrchr(note_path, '\\');

    if (last_sep) {
        size_t dir_len = last_sep - note_path;
        strncpy(dir_path, note_path, dir_len);
        dir_path[dir_len] = '\0';
        strcpy(file_name, last_sep + 1);
    } else {
        strcpy(dir_path, ".");
        strcpy(file_name, note_path);
    }

    // Create .archive directory
    char archive_dir[1024];
    sprintf(archive_dir, "%s%s.archive", dir_path, 
#ifdef _WIN32
    "\\"
#else
    "/"
#endif
    );

    mkdir(archive_dir, 0755);

    // New path
    char new_path[1024];
    sprintf(new_path, "%s%s%s", archive_dir, 
#ifdef _WIN32
    "\\"
#else
    "/"
#endif
    , file_name);

    printf("Archiving: %s -> %s\n", file_name, archive_dir);

    if (rename(note_path, new_path) == 0) {
        printf("Success! Note moved to archive.\n");
    } else {
        perror("Error moving file");
        return 1;
    }

    printf("Press any key to return...");
    getchar();

    return 0;
}
