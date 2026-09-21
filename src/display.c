#define _POSIX_C_SOURCE 200809L

#include "display.h"

#include <ncurses.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

enum {
    COLOR_TIMER = 1,
    COLOR_PAUSED,
    COLOR_FINISHED
};

#define CAR_FRAME_COUNT 4
#define CAR_HEIGHT 5
#define CAR_WIDTH 24
#define CAR_FRAME_NS INT64_C(66666667)

static bool colors_enabled;
static bool was_finished;
static struct timespec blink_started;

static const char car_frames[CAR_FRAME_COUNT][CAR_HEIGHT][CAR_WIDTH + 1] = {
    {
        "        _______         ",
        "   ____/|_||_\\`.__      ",
        "  / _          _   \\___ ",
        " |____________________/>",
        "    (O)          (O)    "
    },
    {
        "        _______         ",
        "   ____/|_||_\\`.__      ",
        "  / _          _   \\___ ",
        ".|____________________/>",
        "    (O)          (O)    "
    },
    {
        "        _______         ",
        "   ____/|_||_\\`.__      ",
        "  / _          _   \\___ ",
        "-|____________________/>",
        "    (O)          (O)    "
    },
    {
        "        _______         ",
        "   ____/|_||_\\`.__      ",
        "  / _          _   \\___ ",
        "'|____________________/>",
        "    (O)          (O)    "
    }
};

typedef struct {
    int width;
    const char *rows[7];
} Glyph;

static const Glyph digits[] = {
    {5, {"01110", "10001", "10011", "10101", "11001", "10001", "01110"}},
    {5, {"00100", "01100", "00100", "00100", "00100", "00100", "01110"}},
    {5, {"01110", "10001", "00001", "00010", "00100", "01000", "11111"}},
    {5, {"11110", "00001", "00001", "01110", "00001", "00001", "11110"}},
    {5, {"00010", "00110", "01010", "10010", "11111", "00010", "00010"}},
    {5, {"11111", "10000", "10000", "11110", "00001", "00001", "11110"}},
    {5, {"01110", "10000", "10000", "11110", "10001", "10001", "01110"}},
    {5, {"11111", "00001", "00010", "00100", "01000", "01000", "01000"}},
    {5, {"01110", "10001", "10001", "01110", "10001", "10001", "01110"}},
    {5, {"01110", "10001", "10001", "01111", "00001", "00001", "01110"}}
};

static const Glyph colon = {1, {"0", "1", "1", "0", "1", "1", "0"}};

static const Glyph *glyph_for(char character)
{
    return character == ':' ? &colon : &digits[character - '0'];
}

static int clock_width(const char *text)
{
    int width = -1;

    for (; *text != '\0'; ++text) {
        width += glyph_for(*text)->width * 2 + 1;
    }
    return width;
}

static void draw_centered_n(int row, const char *text, size_t length,
                            int columns, attr_t attrs)
{
    int shown;
    int column;

    if (row < 0) {
        return;
    }
    shown = length < (size_t)columns ? (int)length : columns;
    column = (columns - shown) / 2;
    attron(attrs);
    mvaddnstr(row, column, text, shown);
    attroff(attrs);
}

static void draw_centered(int row, const char *text, int columns, attr_t attrs)
{
    draw_centered_n(row, text, strlen(text), columns, attrs);
}

static int note_line_count(const char *note)
{
    int lines = *note == '\0' ? 0 : 1;

    for (; *note != '\0'; ++note) {
        lines += *note == '\n';
    }
    return lines;
}

static void draw_notes(int row, const char *note, int columns)
{
    const char *line = note;

    while (*line != '\0') {
        const char *end = strchr(line, '\n');
        size_t length = end == NULL ? strlen(line) : (size_t)(end - line);

        draw_centered_n(row++, line, length, columns, A_BOLD);
        if (end == NULL) {
            break;
        }
        line = end + 1;
    }
}

static bool show_finished_title(void)
{
    struct timespec now;
    long long elapsed_ms;

    if (!was_finished) {
        if (clock_gettime(CLOCK_MONOTONIC, &blink_started) == -1) {
            return true;
        }
        was_finished = true;
    }
    if (clock_gettime(CLOCK_MONOTONIC, &now) == -1) {
        return true;
    }
    elapsed_ms = (now.tv_sec - blink_started.tv_sec) * 1000LL +
                 (now.tv_nsec - blink_started.tv_nsec) / 1000000LL;
    return elapsed_ms / 500 % 2 == 0;
}

static void draw_clock(int row, int column, const char *text)
{
    attr_t attrs = A_BOLD |
                   (colors_enabled ? COLOR_PAIR(COLOR_TIMER) : A_NORMAL);

    for (; *text != '\0'; ++text) {
        const Glyph *glyph = glyph_for(*text);
        int y;

        for (y = 0; y < 7; ++y) {
            int x;

            for (x = 0; x < glyph->width; ++x) {
                if (glyph->rows[y][x] == '1') {
                    mvaddch(row + y, column + x * 2, ' ' | A_REVERSE | attrs);
                    mvaddch(row + y, column + x * 2 + 1,
                            ' ' | A_REVERSE | attrs);
                }
            }
        }
        column += glyph->width * 2 + 1;
    }
}

static void draw_border(int row, int column, int height, int width)
{
    mvaddch(row, column, ACS_ULCORNER);
    mvhline(row, column + 1, ACS_HLINE, width - 2);
    mvaddch(row, column + width - 1, ACS_URCORNER);
    mvvline(row + 1, column, ACS_VLINE, height - 2);
    mvvline(row + 1, column + width - 1, ACS_VLINE, height - 2);
    mvaddch(row + height - 1, column, ACS_LLCORNER);
    mvhline(row + height - 1, column + 1, ACS_HLINE, width - 2);
    mvaddch(row + height - 1, column + width - 1, ACS_LRCORNER);
}

static int car_frame(const Timer *timer)
{
    int64_t elapsed = timer->initial_ns - timer->remaining_ns;

    if (elapsed <= 0) {
        return 0;
    }
    return (int)(elapsed / CAR_FRAME_NS % CAR_FRAME_COUNT);
}

static void draw_car(int row, int column, int frame)
{
    attr_t attrs = A_BOLD |
                   (colors_enabled ? COLOR_PAIR(COLOR_PAUSED) : A_NORMAL);
    int y;

    attron(attrs);
    for (y = 0; y < CAR_HEIGHT; ++y) {
        mvaddnstr(row + y, column, car_frames[frame][y], CAR_WIDTH);
    }
    attroff(attrs);
}

static void draw_road(int row, int left, int track_start, int track_width)
{
    attr_t start_attrs = A_BOLD |
                         (colors_enabled ? COLOR_PAIR(COLOR_TIMER) : A_NORMAL);
    attr_t finish_attrs = A_BOLD |
                          (colors_enabled ? COLOR_PAIR(COLOR_FINISHED) :
                                            A_NORMAL);

    attron(start_attrs);
    mvaddstr(row, left, "START");
    attroff(start_attrs);
    mvaddch(row, left + 6, '|');
    attron(A_DIM);
    mvhline(row, track_start, '=', track_width);
    attroff(A_DIM);
    mvaddch(row, track_start + track_width, '|');
    attron(finish_attrs);
    mvaddstr(row, track_start + track_width + 2, "FINISH");
    attroff(finish_attrs);
}

int display_init(void)
{
    if (initscr() == NULL) {
        return -1;
    }
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
    (void)curs_set(0);

    if (has_colors() && start_color() != ERR) {
        short background = use_default_colors() == ERR ? COLOR_BLACK : -1;

        colors_enabled = true;
        init_pair(COLOR_TIMER, COLOR_GREEN, background);
        init_pair(COLOR_PAUSED, COLOR_YELLOW, background);
        init_pair(COLOR_FINISHED, COLOR_RED, background);
    }
    return 0;
}

void display_draw(Timer *timer, const char *note)
{
    static const char controls[] = "[SPACE/P] Pause   [R] Reset   [Q] Quit";
    long long remaining = timer_remaining_seconds(timer);
    long long hours = remaining / 3600;
    int minutes = (int)(remaining / 60 % 60);
    int seconds = (int)(remaining % 60);
    char clock_text[64];
    char date[11] = "----------";
    int rows;
    int columns;
    int width;
    int box_width;
    int note_lines;
    int layout_height;
    int top;
    int status_row;
    int car_row;
    int road_row;
    int track_width;
    int track_left;
    int track_start;
    int available_distance;
    int car_column;
    time_t now = time(NULL);
    struct tm local;

    (void)snprintf(clock_text, sizeof(clock_text), "%02lld:%02d:%02d",
                   hours, minutes, seconds);
    if (localtime_r(&now, &local) != NULL) {
        (void)strftime(date, sizeof(date), "%Y-%m-%d", &local);
    }

    getmaxyx(stdscr, rows, columns);
    erase();
    width = clock_width(clock_text);
    box_width = width + 4;
    note_lines = note_line_count(note);
    layout_height = 20 + note_lines;
    track_width = columns - 16;

    if (rows < layout_height + 3 || columns < box_width ||
        columns < (int)strlen(controls) || track_width < CAR_WIDTH) {
        draw_centered(rows / 2 - 1, "Terminal too small.", columns, A_BOLD);
        if (rows / 2 < rows) {
            draw_centered(rows / 2, "Please resize the terminal.", columns,
                          A_NORMAL);
        }
        refresh();
        return;
    }

    top = (rows - 2 - layout_height) / 2;
    draw_border(top, (columns - box_width) / 2, 9, box_width);
    draw_clock(top + 1, (columns - width) / 2, clock_text);
    draw_centered(top + 10, date, columns, A_BOLD);
    draw_notes(top + 12, note, columns);
    status_row = top + 13 + note_lines;

    if (timer->finished) {
        if (show_finished_title()) {
            draw_centered(status_row, "TIME'S UP!", columns,
                          A_BOLD |
                          (colors_enabled ? COLOR_PAIR(COLOR_FINISHED) :
                                            A_NORMAL));
        }
    } else if (timer->paused) {
        was_finished = false;
        draw_centered(status_row, "PAUSED", columns,
                      A_BOLD | (colors_enabled ? COLOR_PAIR(COLOR_PAUSED) :
                                A_NORMAL));
    } else {
        was_finished = false;
    }

    car_row = status_row + 1;
    road_row = car_row + CAR_HEIGHT;
    track_left = (columns - (track_width + 15)) / 2;
    track_start = track_left + 7;
    available_distance = track_width - CAR_WIDTH;
    car_column = track_start +
                 (int)(timer_progress(timer) * available_distance + 0.5);
    draw_car(car_row, car_column, car_frame(timer));
    draw_road(road_row, track_left, track_start, track_width);

    draw_centered(rows - 2, controls, columns, A_DIM);
    refresh();
}

void display_shutdown(void)
{
    endwin();
}
