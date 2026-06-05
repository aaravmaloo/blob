#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define chdir _chdir
#else
#include <unistd.h>
#endif

void trim_newline(char *s) {
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) {
        s[--len] = '\0';
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <note_path>\n", argv[0]);
        return 1;
    }

    char *note_path = argv[1];
    char dir_path[1024];
    char file_name[256];

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

    if (chdir(dir_path) != 0) {
        perror("Error changing to notes directory");
        return 1;
    }

    printf("--- Git Sync: %s ---\n", file_name);

    char branch[64] = "main";
    int is_new_repo = 0;

    // 1. Check if Git is initialized
    if (system("git rev-parse --is-inside-work-tree >nul 2>&1") != 0) {
        printf("Git is not initialized in this directory.\n");
        printf("Initialize new repository here? (y/n): ");
        char choice[10];
        if (fgets(choice, sizeof(choice), stdin) && (choice[0] == 'y' || choice[0] == 'Y')) {
            system("git init");
            is_new_repo = 1;
            
            printf("Enter default branch name [main]: ");
            char b_input[64];
            if (fgets(b_input, sizeof(b_input), stdin)) {
                trim_newline(b_input);
                if (strlen(b_input) > 0) strcpy(branch, b_input);
            }
            
            char b_cmd[128];
            snprintf(b_cmd, sizeof(b_cmd), "git branch -M %s", branch);
            system(b_cmd);

            FILE *g = fopen(".gitignore", "a");
            if (g) {
                fseek(g, 0, SEEK_END);
                if (ftell(g) == 0) {
                    fprintf(g, "*.exe\n*.o\n*.obj\n.DS_Store\ntemp_*\n");
                    printf("Created default .gitignore\n");
                }
                fclose(g);
            }
            system("git add .gitignore");
        } else {
            printf("Aborted.\nPress any key to return...");
            getchar();
            return 1;
        }
    } else {
        // Detect current branch
        FILE *fp = _popen("git rev-parse --abbrev-ref HEAD", "r");
        if (fp) {
            if (fgets(branch, sizeof(branch), fp)) {
                trim_newline(branch);
            }
            _pclose(fp);
        }
    }

    // 2. Check for Remote
    if (system("git remote | findstr . >nul 2>&1") != 0) {
        printf("\nNo remote repository configured.\n");
        printf("Enter remote URL: ");
        char url[1024];
        if (fgets(url, sizeof(url), stdin)) {
            trim_newline(url);
            if (strlen(url) > 0) {
                char cmd[1100];
                snprintf(cmd, sizeof(cmd), "git remote add origin %s", url);
                system(cmd);
                printf("Remote 'origin' added.\n");
            }
        }
    }

    // 3. Sync Flow
    char cmd[2048];

    snprintf(cmd, sizeof(cmd), "git add \"%s\"", file_name);
    system(cmd);

    snprintf(cmd, sizeof(cmd), "git commit -m \"blob: update %s\"", file_name);
    // system() returns non-zero if there's nothing to commit, which is fine
    system(cmd);

    // Pull from remote
    printf("\nSyncing with remote (pull --rebase)...\n");
    snprintf(cmd, sizeof(cmd), "git pull --rebase origin %s", branch);
    if (system(cmd) != 0) {
        printf("Note: Initial pull failed or no remote changes. Continuing...\n");
    }

    // Push to remote
    printf("Pushing to remote...\n");
    if (is_new_repo) {
        snprintf(cmd, sizeof(cmd), "git push -u origin %s", branch);
    } else {
        snprintf(cmd, sizeof(cmd), "git push origin %s", branch);
    }
    
    if (system(cmd) != 0) {
        printf("\nError: git push failed. Check your remote settings or credentials.\n");
    } else {
        printf("\nSuccess! Everything is up to date.\n");
    }

    printf("---------------------------\n");
    printf("Press any key to return...");
    getchar();

    return 0;
}
