#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <ctype.h>

#define MAX_FILES 4096
#define PATH_MAX_SIZE 512
#define VISIBLE_ROWS 20
#define INPUT_MAX 256

#define COLOR_BLUE   "\033[1;34m"
#define COLOR_GREEN  "\033[32m"
#define COLOR_DIM    "\033[2m"
#define COLOR_RESET  "\033[0m"
#define STYLE_INVERT "\033[7m"

#define KEY_UP    1000
#define KEY_DOWN  1001
#define KEY_ESC   1002

typedef struct {
    char name[256];
    int is_dir;
    off_t size;
    mode_t mode;
} FileEntry;

typedef enum {
    MODE_NORMAL,
    MODE_FILTER,
    MODE_MKDIR
} UiMode;

static int show_hidden = 0;
static char filter[INPUT_MAX];
static char input_buf[INPUT_MAX];
static UiMode ui_mode = MODE_NORMAL;

static void enable_raw_mode(struct termios *orig_termios) {
    struct termios raw = *orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

static void disable_raw_mode(struct termios *orig_termios) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, orig_termios);
}

static int str_contains_ci(const char *haystack, const char *needle) {
    size_t nlen = strlen(needle);
    if (nlen == 0) return 1;

    for (const char *p = haystack; *p; p++) {
        size_t i = 0;
        while (i < nlen && p[i] && tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) {
            i++;
        }
        if (i == nlen) return 1;
    }
    return 0;
}

static void human_size(off_t bytes, char *buf, size_t buflen) {
    const char *units[] = {"B", "K", "M", "G", "T"};
    double size = (double)bytes;
    int unit = 0;

    while (size >= 1024.0 && unit < 4) {
        size /= 1024.0;
        unit++;
    }

    if (unit == 0) {
        snprintf(buf, buflen, "%lld B", (long long)bytes);
    } else {
        snprintf(buf, buflen, "%.1f %s", size, units[unit]);
    }
}

static void format_mode(mode_t mode, char *buf) {
    buf[0] = (mode & S_IRUSR) ? 'r' : '-';
    buf[1] = (mode & S_IWUSR) ? 'w' : '-';
    buf[2] = (mode & S_IXUSR) ? 'x' : '-';
    buf[3] = (mode & S_IRGRP) ? 'r' : '-';
    buf[4] = (mode & S_IWGRP) ? 'w' : '-';
    buf[5] = (mode & S_IXGRP) ? 'x' : '-';
    buf[6] = (mode & S_IROTH) ? 'r' : '-';
    buf[7] = (mode & S_IWOTH) ? 'w' : '-';
    buf[8] = (mode & S_IXOTH) ? 'x' : '-';
    buf[9] = '\0';
}

static int entry_cmp(const void *a, const void *b) {
    const FileEntry *fa = a;
    const FileEntry *fb = b;

    if (fa->is_dir != fb->is_dir) {
        return fb->is_dir - fa->is_dir;
    }
    return strcasecmp(fa->name, fb->name);
}

static int scan_directory(const char *path, FileEntry files[]) {
    DIR *dir = opendir(path);
    if (!dir) return -1;

    int count = 0;
    struct dirent *entry;
    struct stat file_stat;
    char full_path[PATH_MAX_SIZE];

    strncpy(files[count].name, "..", sizeof(files[count].name));
    files[count].is_dir = 1;
    files[count].size = 0;
    files[count].mode = 0755;
    count++;

    while ((entry = readdir(dir)) != NULL && count < MAX_FILES) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (!show_hidden && entry->d_name[0] == '.') {
            continue;
        }

        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
        if (stat(full_path, &file_stat) != 0) {
            continue;
        }

        strncpy(files[count].name, entry->d_name, sizeof(files[count].name));
        files[count].is_dir = S_ISDIR(file_stat.st_mode);
        files[count].size = file_stat.st_size;
        files[count].mode = file_stat.st_mode;
        count++;
    }

    closedir(dir);

    if (count > 1) {
        qsort(files + 1, (size_t)(count - 1), sizeof(FileEntry), entry_cmp);
    }
    return count;
}

static int rebuild_visible(const FileEntry files[], int file_count, int visible[]) {
    int visible_count = 0;

    for (int i = 0; i < file_count; i++) {
        if (filter[0] && !str_contains_ci(files[i].name, filter)) {
            continue;
        }
        visible[visible_count++] = i;
    }
    return visible_count;
}

static int read_key(void) {
    int c = getchar();
    if (c != 27) return c;

    if (getchar() != 91) return KEY_ESC;

    switch (getchar()) {
        case 'A': return KEY_UP;
        case 'B': return KEY_DOWN;
        default:  return KEY_ESC;
    }
}

static void open_with_system(const char *path) {
    pid_t pid = fork();
    if (pid == 0) {
        execlp("xdg-open", "xdg-open", path, (char *)NULL);
        execlp("open", "open", path, (char *)NULL);
        _exit(127);
    }
    if (pid > 0) {
        waitpid(pid, NULL, WNOHANG);
    }
}

static int enter_directory(const char *name, char current_path[], FileEntry files[]) {
    if (chdir(name) != 0) {
        return -1;
    }
    if (getcwd(current_path, PATH_MAX_SIZE) == NULL) {
        return -1;
    }
    return scan_directory(current_path, files);
}

static void draw_interface(const char *current_path, const FileEntry files[], const int visible[],
                           int visible_count, int selected, int scroll_offset) {
    printf("\033[2J\033[H");
    printf("--- nodus v0.2.0 | %s ---\n", current_path);
    printf(COLOR_DIM
           " j/k/↑/↓ navigate  Enter open  h/Backspace up  g/G top/bottom  / filter  . hidden  "
           "n mkdir  r refresh  q quit\n"
           COLOR_RESET);

    if (ui_mode == MODE_FILTER) {
        printf(" filter: %s_\n", filter);
    } else if (ui_mode == MODE_MKDIR) {
        printf(" new directory: %s_\n", input_buf);
    } else if (filter[0]) {
        printf(COLOR_DIM " filter active: %s (Esc to clear)\n" COLOR_RESET, filter);
    } else {
        printf("\n");
    }

    int rows = visible_count < VISIBLE_ROWS ? visible_count : VISIBLE_ROWS;
    for (int row = 0; row < rows; row++) {
        int idx = visible[scroll_offset + row];
        int is_selected = (scroll_offset + row) == selected;

        if (is_selected) {
            printf(" -> " STYLE_INVERT);
        } else {
            printf("    ");
        }

        const FileEntry *entry = &files[idx];
        const char *color = COLOR_RESET;
        if (entry->is_dir) {
            color = COLOR_BLUE;
        } else if (entry->mode & (S_IXUSR | S_IXGRP | S_IXOTH)) {
            color = COLOR_GREEN;
        }

        char size_buf[32];
        if (entry->is_dir) {
            snprintf(size_buf, sizeof(size_buf), "   dir");
        } else {
            human_size(entry->size, size_buf, sizeof(size_buf));
        }

        printf("%s%-32s %8s%s", color, entry->name, size_buf, COLOR_RESET);

        if (entry->is_dir) {
            printf("/");
        }

        if (is_selected) {
            printf(COLOR_RESET);
        }
        printf("\n");
    }

    if (visible_count == 0) {
        printf(COLOR_DIM " (no matches)\n" COLOR_RESET);
    } else if (visible_count > VISIBLE_ROWS) {
        printf(COLOR_DIM " showing %d-%d of %d\n" COLOR_RESET,
               scroll_offset + 1,
               scroll_offset + rows,
               visible_count);
    } else {
        printf("\n");
    }

    if (visible_count > 0 && selected >= 0 && selected < visible_count) {
        const FileEntry *sel = &files[visible[selected]];
        char mode_buf[16];
        char size_buf[32];
        format_mode(sel->mode, mode_buf);
        human_size(sel->size, size_buf, sizeof(size_buf));
        printf(COLOR_DIM " %d items | selected: %s%s | %s | %s\n" COLOR_RESET,
               visible_count,
               sel->name,
               sel->is_dir ? "/" : "",
               sel->is_dir ? "dir" : size_buf,
               mode_buf);
    }
}

static void clamp_selection(int *selected, int *scroll_offset, int visible_count) {
    if (visible_count == 0) {
        *selected = 0;
        *scroll_offset = 0;
        return;
    }
    if (*selected < 0) *selected = 0;
    if (*selected >= visible_count) *selected = visible_count - 1;
    if (*selected < *scroll_offset) {
        *scroll_offset = *selected;
    }
    if (*selected >= *scroll_offset + VISIBLE_ROWS) {
        *scroll_offset = *selected - VISIBLE_ROWS + 1;
    }
    if (*scroll_offset < 0) *scroll_offset = 0;
    if (*scroll_offset > visible_count - 1) {
        *scroll_offset = visible_count > VISIBLE_ROWS ? visible_count - VISIBLE_ROWS : 0;
    }
}

int main(void) {
    char current_path[PATH_MAX_SIZE];
    if (getcwd(current_path, sizeof(current_path)) == NULL) {
        perror("nodus: unable to get current working directory");
        return 1;
    }

    FileEntry files[MAX_FILES];
    int visible[MAX_FILES];
    int file_count = scan_directory(current_path, files);
    if (file_count < 0) {
        perror("nodus: unable to read directory");
        return 1;
    }

    struct termios orig_termios;
    tcgetattr(STDIN_FILENO, &orig_termios);
    enable_raw_mode(&orig_termios);

    int visible_count = rebuild_visible(files, file_count, visible);
    int selected = 0;
    int scroll_offset = 0;

    while (1) {
        clamp_selection(&selected, &scroll_offset, visible_count);
        draw_interface(current_path, files, visible, visible_count, selected, scroll_offset);

        int key = read_key();

        if (ui_mode == MODE_FILTER) {
            if (key == KEY_ESC || key == 27) {
                filter[0] = '\0';
                ui_mode = MODE_NORMAL;
                visible_count = rebuild_visible(files, file_count, visible);
                selected = 0;
                scroll_offset = 0;
            } else if (key == '\n') {
                ui_mode = MODE_NORMAL;
                visible_count = rebuild_visible(files, file_count, visible);
                selected = 0;
                scroll_offset = 0;
            } else if (key == 127 || key == 8) {
                size_t len = strlen(filter);
                if (len > 0) filter[len - 1] = '\0';
                visible_count = rebuild_visible(files, file_count, visible);
                selected = 0;
                scroll_offset = 0;
            } else if (key >= 32 && key <= 126 && strlen(filter) + 1 < INPUT_MAX) {
                size_t len = strlen(filter);
                filter[len] = (char)key;
                filter[len + 1] = '\0';
                visible_count = rebuild_visible(files, file_count, visible);
                selected = 0;
                scroll_offset = 0;
            }
            continue;
        }

        if (ui_mode == MODE_MKDIR) {
            if (key == KEY_ESC || key == 27) {
                input_buf[0] = '\0';
                ui_mode = MODE_NORMAL;
            } else if (key == '\n') {
                if (input_buf[0]) {
                    if (mkdir(input_buf, 0755) != 0) {
                        disable_raw_mode(&orig_termios);
                        perror("nodus: mkdir failed");
                        enable_raw_mode(&orig_termios);
                    } else {
                        file_count = scan_directory(current_path, files);
                        visible_count = rebuild_visible(files, file_count, visible);
                    }
                }
                input_buf[0] = '\0';
                ui_mode = MODE_NORMAL;
            } else if (key == 127 || key == 8) {
                size_t len = strlen(input_buf);
                if (len > 0) input_buf[len - 1] = '\0';
            } else if (key >= 32 && key <= 126 && strlen(input_buf) + 1 < INPUT_MAX) {
                size_t len = strlen(input_buf);
                input_buf[len] = (char)key;
                input_buf[len + 1] = '\0';
            }
            continue;
        }

        if (key == 'q') {
            break;
        } else if (key == 'j' || key == KEY_DOWN) {
            selected++;
        } else if (key == 'k' || key == KEY_UP) {
            selected--;
        } else if (key == 'g') {
            selected = 0;
            scroll_offset = 0;
        } else if (key == 'G') {
            selected = visible_count > 0 ? visible_count - 1 : 0;
        } else if (key == 'r') {
            file_count = scan_directory(current_path, files);
            visible_count = rebuild_visible(files, file_count, visible);
        } else if (key == '.') {
            show_hidden = !show_hidden;
            file_count = scan_directory(current_path, files);
            visible_count = rebuild_visible(files, file_count, visible);
            selected = 0;
            scroll_offset = 0;
        } else if (key == '/') {
            ui_mode = MODE_FILTER;
            filter[0] = '\0';
        } else if (key == 'n') {
            ui_mode = MODE_MKDIR;
            input_buf[0] = '\0';
        } else if (key == KEY_ESC || key == 27) {
            if (filter[0]) {
                filter[0] = '\0';
                visible_count = rebuild_visible(files, file_count, visible);
                selected = 0;
                scroll_offset = 0;
            }
        } else if (key == 'h' || key == 127 || key == 8) {
            if (strcmp(current_path, "/") != 0) {
                int count = enter_directory("..", current_path, files);
                if (count >= 0) {
                    file_count = count;
                    visible_count = rebuild_visible(files, file_count, visible);
                    selected = 0;
                    scroll_offset = 0;
                }
            }
        } else if (key == '\n') {
            if (visible_count == 0) continue;

            const FileEntry *entry = &files[visible[selected]];
            if (entry->is_dir) {
                int count = enter_directory(entry->name, current_path, files);
                if (count >= 0) {
                    file_count = count;
                    visible_count = rebuild_visible(files, file_count, visible);
                    selected = 0;
                    scroll_offset = 0;
                }
            } else {
                char full_path[PATH_MAX_SIZE];
                if (snprintf(full_path, sizeof(full_path), "%s/%s", current_path, entry->name)
                    >= (int)sizeof(full_path)) {
                    continue;
                }
                open_with_system(full_path);
            }
        }
    }

    printf("\033[2J\033[H");
    disable_raw_mode(&orig_termios);
    printf("Exiting nodus.\n");

    return 0;
}
