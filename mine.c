// mines.c -- cross-platform (Linux + Windows) minimal Minesweeper terminal demo
// Build:
//   Linux: gcc -std=c11 -O2 -o mines mines.c
//   Windows (MSVC): cl /EHsc mines.c
//   Windows (mingw): x86_64-w64-mingw32-gcc -std=c11 -O2 -o mines.exe mines.c

#include <stdio.h>
#include <assert.h>
#include <stdbool.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#if defined(_WIN32)
#define IS_WINDOWS 1
#include <windows.h>
#include <conio.h>
#else
#define IS_WINDOWS 0
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#endif

typedef enum {
    EMPTY,
    MINE,
    COUNT
} Cell_Type;

typedef struct {
    Cell_Type type;
    bool open, flag;
} Cell;

#define COLS 10
#define ROWS 10

typedef struct {
    Cell cells[ROWS*COLS];
    size_t cur_row, cur_col;
    size_t mines_count;
    size_t open_cells_count;
} Grid;

static inline int max_i(int a, int b) { return a > b ? a : b; }
static inline int min_i(int a, int b) { return a < b ? a : b; }
static inline int randi(int max_inclusive) {
    return rand() % (max_inclusive + 1);
}

Cell cell_at(const Grid* grid, size_t row, size_t col) {
    size_t idx = row * COLS + col;
    assert(idx < ROWS*COLS);
    return grid->cells[idx];
}

void set_cell(Grid* grid, size_t row, size_t col, const Cell_Type c) {
    size_t idx = row * COLS + col;
    grid->cells[idx].type = c;
}

void clear_grid(Grid* grid) {
    for (size_t i = 0; i < ROWS*COLS; ++i) {
        grid->cells[i].type = EMPTY;
        grid->cells[i].flag = false;
        grid->cells[i].open = false;
    }
    grid->cur_row = 0;
    grid->cur_col = 0;
    grid->open_cells_count = 0;
}

void randomize_grid(Grid* grid) {
    // place mines_count mines randomly without duplicates
    size_t placed = 0;
    while (placed < grid->mines_count) {
        int rr = randi(ROWS - 1);
        int cc = randi(COLS - 1);
        if (cell_at(grid, rr, cc).type != MINE) {
            set_cell(grid, rr, cc, MINE);
            placed++;
        }
    }
}

void init_grid(Grid* grid, size_t mines_count) {
    clear_grid(grid);
    grid->mines_count = mines_count;
    randomize_grid(grid);
}

size_t count_nbors(const Grid* grid, size_t row, size_t col) {
    size_t nbors = 0;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) continue;
            int r = (int)row + dy;
            int c = (int)col + dx;
            if (r < 0 || r >= ROWS || c < 0 || c >= COLS) continue;
            if (cell_at(grid, (size_t)r, (size_t)c).type == MINE) nbors++;
        }
    }
    return nbors;
}

#if IS_WINDOWS
// Enable ANSI sequences on recent Windows 10+ consoles if possible.
static void enable_ansi_windows(void) {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD mode;
    if (!GetConsoleMode(h, &mode)) return;
    mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(h, mode);
}
#endif

static void clear_terminal_and_move_up(int rows) {
    // Use ANSI to move cursor up and left; this works on Linux and Windows (if ANSI enabled).
    // We print enough to overwrite previous grid. Caller prints grid then sleeps for next draw.
    printf("\033[%dA", rows);
}

void draw_grid(const Grid* grid) {
    // Header
    printf("%dx%d | mines: %zu | open: %zu/%d\n", COLS, ROWS, grid->mines_count, grid->open_cells_count, ROWS*COLS);

    for (int c = 0; c < COLS*3 + 2; ++c) putchar('-');
    putchar('\n');

    for (int r = 0; r < ROWS; ++r) {
        putchar('|');
        for (int c = 0; c < COLS; ++c) {
            size_t idx = r * COLS + c;
            Cell cell = cell_at(grid, r, c);
            putchar(grid->cur_row == (size_t)r && grid->cur_col == (size_t)c ? '[' : ' ');
            if (!cell.open) {
                if (cell.flag) putchar('F');
                else putchar('#');
            } else {
                switch (cell.type) {
                    case EMPTY: {
                        size_t nbors = count_nbors(grid, r, c);
                        putchar(nbors == 0 ? ' ' : '0' + (int)nbors);
                    } break;
                    case MINE:
                        putchar('*');
                        break;
                    case COUNT:
                    default:
                        putchar('?');
                        break;
                }
            }
            putchar(grid->cur_row == (size_t)r && grid->cur_col == (size_t)c ? ']' : ' ');
        }
        printf("|\n");
    }

    for (int c = 0; c < COLS*3 + 2; ++c) putchar('-');
    putchar('\n');
    fflush(stdout);
}

#if IS_WINDOWS
// Windows: non-blocking key read using _kbhit() and _getch()
// But we want blocking single-key read like original. Use _getch() which blocks.
static int platform_readkey(void) {
    int ch = _getch();
    // _getch may return 0 or 0xE0 for special keys; ignore and read next for simplicity
    if (ch == 0 || ch == 0xE0) ch = _getch();
    return ch;
}
#else
// POSIX: set terminal raw mode and read single byte
static struct termios oldt;
static bool term_inited = false;

static void enable_raw_mode(void) {
    struct termios tattr;
    tcgetattr(STDIN_FILENO, &oldt);
    tattr = oldt;
    tattr.c_lflag &= ~(ICANON | ECHO);
    tattr.c_cc[VMIN] = 1;
    tattr.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &tattr);
    term_inited = true;
}

static void disable_raw_mode(void) {
    if (term_inited) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &oldt);
        term_inited = false;
    }
}

static int platform_readkey(void) {
    unsigned char ch = 0;
    if (read(STDIN_FILENO, &ch, 1) == 1) return (int)ch;
    return -1;
}
#endif

int main(void) {
    srand((unsigned int)time(NULL));
    Grid grid;
    size_t max_mines = 25;
    init_grid(&grid, max_mines);

#if IS_WINDOWS
    // Enable ANSI support on Windows consoles if possible
    enable_ansi_windows();
#else
    // ensure stdin is a TTY
    if (!isatty(STDIN_FILENO)) {
        fprintf(stderr, "ERROR: stdin is not a terminal!\n");
        return 1;
    }
    enable_raw_mode();
#endif

    bool quit = false;
    // Initial draw
    draw_grid(&grid);

    while (!quit) {
        int ch = platform_readkey();
        if (ch < 0) continue;

        switch (ch) {
            case 'd': if (grid.cur_col < COLS-1) grid.cur_col++; break;
            case 'a': if (grid.cur_col > 0) grid.cur_col--; break;
            case 's': if (grid.cur_row < ROWS-1) grid.cur_row++; break;
            case 'w': if (grid.cur_row > 0) grid.cur_row--; break;
            case ' ': {
                size_t idx = grid.cur_row * COLS + grid.cur_col;
                assert(idx < COLS*ROWS);
                if (!grid.cells[idx].open) {
                    grid.cells[idx].open = true;
                    grid.open_cells_count++;
                }
                if (grid.cells[idx].type == MINE) {
                    // reveal all
                    for (int r = 0; r < ROWS; ++r) {
                        for (int c = 0; c < COLS; ++c) {
                            size_t i = r * COLS + c;
                            if (!grid.cells[i].open) {
                                grid.cells[i].open = true;
                                grid.open_cells_count++;
                            }
                        }
                    }
                }
            } break;
            case 'f': {
                size_t idx = grid.cur_row * COLS + grid.cur_col;
                assert(idx < COLS*ROWS);
                grid.cells[idx].flag = !grid.cells[idx].flag;
            } break;
            case 'q': quit = true; break;
            case 'r': init_grid(&grid, max_mines); break;
            default: break;
        }

        // Move cursor up to overwrite previous grid. We printed ROWS + 3 lines (header + top border + ROWS + bottom border)
        // That estimate: header (1) + border (1) + ROWS lines + border (1) => ROWS + 3
        clear_terminal_and_move_up(ROWS + 3);
        draw_grid(&grid);
    }

#if !IS_WINDOWS
    disable_raw_mode();
#endif

    return 0;
}

