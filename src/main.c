#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#define MAX_FILES 1024
#define PATH_MAX_SIZE 512

#define COLOR_BLUE    "\033[1;34m"
#define COLOR_DEFAULT "\033[0m"
#define STYLE_INVERT  "\033[7m"

typedef struct {
    char name[256];
    int is_dir;
} FileEntry;

void enable_raw_mode(struct termios *orig_termios) {
    struct termios raw = *orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void disable_raw_mode(struct termios *orig_termios) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, orig_termios);
}

// Scans the current path and packs the global array with fresh entries
int scan_directory(const char *path, FileEntry files[]) {
    DIR *dir = opendir(path);
    if (!dir) {
        return -1; // Fail silently or handle error gracefully in UI
    }

    int count = 0;
    struct dirent *entry;
    struct stat file_stat;
    char full_path[PATH_MAX_SIZE];

    // Always keep parent directory navigation option at index 0
    strncpy(files[count].name, "..", sizeof(files[count].name));
    files[count].is_dir = 1;
    count++;

    while ((entry = readdir(dir)) != NULL && count < MAX_FILES) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);

        if (stat(full_path, &file_stat) == 0) {
            strncpy(files[count].name, entry->d_name, sizeof(files[count].name));
            files[count].is_dir = S_ISDIR(file_stat.st_mode);
            count++;
        }
    }
    closedir(dir);
    return count;
}

void draw_interface(const char *current_path, FileEntry files[], int file_count, int selected) {
    printf("\033[2J\033[H");
    printf("--- nodus v0.1.0 | Current path: %s ---\n", current_path);
    printf(" 'j'/'k' -> Navigate | 'Enter' -> Open / Enter Directory | 'q' -> Quit\n\n");

    for (int i = 0; i < file_count; i++) {
        if (i == selected) {
            printf(" -> " STYLE_INVERT);
        } else {
            printf("    ");
        }

        if (files[i].is_dir) {
            printf(COLOR_BLUE "%s/" COLOR_DEFAULT, files[i].name);
        } else {
            printf("%s", files[i].name);
        }

        if (i == selected) {
            printf(COLOR_DEFAULT "\n");
        } else {
            printf("\n");
        }
    }
}

int main() {
    // Maintain a mutable string buffer tracking the current working directory path
    char current_path[PATH_MAX_SIZE];
    if (getcwd(current_path, sizeof(current_path)) == NULL) {
        perror("nodus: Unable to get current working directory");
        return 1;
    }

    FileEntry files[MAX_FILES];
    int file_count = scan_directory(current_path, files);
    if (file_count < 0) return 1;

    struct termios orig_termios;
    tcgetattr(STDIN_FILENO, &orig_termios);
    enable_raw_mode(&orig_termios);

    int selected = 0;
    char ch;

    while (1) {
        draw_interface(current_path, files, file_count, selected);

        ch = getchar();

        if (ch == 'q') {
            break;
        } else if (ch == 'j') {
            if (selected < file_count - 1) selected++;
        } else if (ch == 'k') {
            if (selected > 0) selected--;
        } 
        // Catching the Enter Key (Carriage Return '\n')
        else if (ch == '\n') {
            // Only trigger directory swapping if the highlighted entry is a folder
            if (files[selected].is_dir) {
                // Execute system level directory shift
                if (chdir(files[selected].name) == 0) {
                    // Refresh our dynamic string track buffer to match the new spot
                    getcwd(current_path, sizeof(current_path));
                    
                    // Rescan everything from the new point block location
                    file_count = scan_directory(current_path, files);
                    
                    // Reset selector index back to top so it doesn't overflow bounds
                    selected = 0; 
                }
            }
        }
    }

    printf("\033[2J\033[H");
    disable_raw_mode(&orig_termios);
    printf("Exiting nodus safely.\n");

    return 0;
}
