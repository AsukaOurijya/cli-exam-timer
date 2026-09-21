#define _POSIX_C_SOURCE 200809L

#include "display.h"
#include "timer.h"

#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <ncurses.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define NOTE_SIZE 4096
#define NOTE_LINE_SIZE 512
#define MAX_SECONDS (INT64_MAX / INT64_C(1000000000))
#define MAX_HOURS (MAX_SECONDS / 3600)
#define TITLE_HEIGHT 7
#define TITLE_WIDTH 95
#define SETUP_HEIGHT (TITLE_HEIGHT + 10)

typedef struct {
    long long hours;
    long long minutes;
    long long seconds;
    char note[NOTE_SIZE];
} Config;

typedef struct {
    int field;
    long long defaults[3];
    char entered[3][64];
    char note[NOTE_SIZE];
    int note_number;
    bool note_blank;
    char note_choice;
    const char *input;
    size_t input_length;
    char message[NOTE_LINE_SIZE + 80];
} Setup;

static const char title[TITLE_HEIGHT][TITLE_WIDTH + 1] = {
    "    __  _      ____        ___  __ __   ____  ___ ___      ______  ____  ___ ___    ___  ____  ",
    "   /  ]| T    l    j      /  _]|  T  T /    T|   T   T    |      Tl    j|   T   T  /  _]|    \\",
    "  /  / | |     |  T      /  [_ |  |  |Y  o  || _   _ |    |      | |  T | _   _ | /  [_ |  D  )",
    " /  /  | l___  |  |     Y    _]l_   _j|     ||  \\_/  |    l_j  l_j |  | |  \\_/  |Y    _]|    /",
    "/   \\_ |     T |  |     |   [_ |     ||  _  ||   |   |      |  |   |  | |   |   ||   [_ |    \\",
    "\\     ||     | j  l     |     T|  |  ||  |  ||   |   |      |  |   j  l |   |   ||     T|  .  Y",
    " \\____jl_____j|____j    l_____j|__j__|l__j__jl___j___j      l__j  |____jl___j___jl_____jl__j\\_j"
};
static const char *const number_labels[] = {"Hours", "Minutes", "Seconds"};
static const char note_heading[] =
    "Note [Click enter to new line, click enter twice to proceed to timer]:";
static const char note_question[] =
    "Would you like to add additional notes? (y/n)?";

static volatile sig_atomic_t interrupted;

static void handle_signal(int signal_number)
{
    (void)signal_number;
    interrupted = 1;
}

static void usage(FILE *stream, const char *program)
{
    fprintf(stream,
            "Usage: %s [-H HOURS] [-M MINUTES] [-S SECONDS] [-n NOTE]\n"
            "\n"
            "  -H, --hours      hours (0-%lld)\n"
            "  -M, --minutes    minutes (0-59)\n"
            "  -S, --seconds    seconds (0-59)\n"
            "  -n, --note       note shown below the date\n"
            "  -h, --help       show this help\n",
            program, (long long)MAX_HOURS);
}

static bool parse_number_value(const char *text, long long maximum,
                               long long *value)
{
    char *end;
    long long parsed;

    errno = 0;
    parsed = strtoll(text, &end, 10);
    if (errno != 0 || text == end || *end != '\0' || parsed < 0 ||
        parsed > maximum) {
        return false;
    }
    *value = parsed;
    return true;
}

static int parse_number(const char *text, long long maximum,
                        const char *name, long long *value)
{
    if (!parse_number_value(text, maximum, value)) {
        fprintf(stderr, "Invalid %s '%s': expected 0-%lld.\n",
                name, text, maximum);
        return -1;
    }
    return 0;
}

static int copy_note(char destination[NOTE_SIZE], const char *source)
{
    size_t length = strlen(source);

    if (length >= NOTE_SIZE) {
        fprintf(stderr, "Note is too long (maximum %d bytes).\n",
                NOTE_SIZE - 1);
        return -1;
    }
    memcpy(destination, source, length + 1);
    return 0;
}

static int parse_arguments(int argc, char **argv, Config *config)
{
    static const struct option options[] = {
        {"hours", required_argument, NULL, 'H'},
        {"minutes", required_argument, NULL, 'M'},
        {"seconds", required_argument, NULL, 'S'},
        {"note", required_argument, NULL, 'n'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0}
    };
    bool duration_seen = false;
    int option;

    opterr = 0;
    while ((option = getopt_long(argc, argv, "H:M:S:n:h", options, NULL)) != -1) {
        switch (option) {
        case 'H':
            if (!duration_seen) {
                config->hours = config->minutes = config->seconds = 0;
                duration_seen = true;
            }
            if (parse_number(optarg, MAX_HOURS, "hours", &config->hours) == -1) {
                return -1;
            }
            break;
        case 'M':
            if (!duration_seen) {
                config->hours = config->minutes = config->seconds = 0;
                duration_seen = true;
            }
            if (parse_number(optarg, 59, "minutes", &config->minutes) == -1) {
                return -1;
            }
            break;
        case 'S':
            if (!duration_seen) {
                config->hours = config->minutes = config->seconds = 0;
                duration_seen = true;
            }
            if (parse_number(optarg, 59, "seconds", &config->seconds) == -1) {
                return -1;
            }
            break;
        case 'n':
            if (copy_note(config->note, optarg) == -1) {
                return -1;
            }
            break;
        case 'h':
            usage(stdout, argv[0]);
            return 1;
        default:
            fprintf(stderr, "Unknown or incomplete option.\n");
            usage(stderr, argv[0]);
            return -1;
        }
    }
    if (optind != argc) {
        fprintf(stderr, "Unexpected argument: %s\n", argv[optind]);
        return -1;
    }
    return 0;
}

static void discard_line(void)
{
    int character;

    while ((character = getchar()) != '\n' && character != EOF) {
    }
}

static int prompt_number(const char *label, long long maximum, long long *value)
{
    char input[64];
    long long parsed;

    for (;;) {
        printf("%s [Default=%02lld]: ", label, *value);
        fflush(stdout);
        if (fgets(input, sizeof(input), stdin) == NULL) {
            fputc('\n', stderr);
            return -1;
        }
        if (strchr(input, '\n') == NULL) {
            discard_line();
            fprintf(stderr, "Input is too long.\n");
            continue;
        }
        input[strcspn(input, "\n")] = '\0';
        if (input[0] == '\0') {
            return 0;
        }
        if (parse_number(input, maximum, label, &parsed) == 0) {
            *value = parsed;
            return 0;
        }
    }
}

static int prompt_notes(Config *config)
{
    char note[NOTE_SIZE] = "";
    char input[NOTE_LINE_SIZE];
    size_t used = 0;
    int number = 1;
    bool blank = false;

    puts(note_heading);
    for (;;) {
        printf(blank ? "   " : "%d. ", number);
        fflush(stdout);
        if (fgets(input, sizeof(input), stdin) == NULL) {
            fputc('\n', stderr);
            return -1;
        }
        if (strchr(input, '\n') == NULL) {
            discard_line();
            fprintf(stderr, "Note line is too long (maximum %d bytes).\n",
                    NOTE_LINE_SIZE - 2);
            continue;
        }
        input[strcspn(input, "\n")] = '\0';
        if (input[0] == '\0') {
            if (blank) {
                memcpy(config->note, note, used + 1);
                return 0;
            }
            blank = true;
            continue;
        }

        blank = false;
        {
            int written = snprintf(note + used, NOTE_SIZE - used,
                                   "%s%d. %s", used == 0 ? "" : "\n",
                                   number, input);

            if (written < 0 || (size_t)written >= NOTE_SIZE - used) {
                fprintf(stderr, "Notes are too long (maximum %d bytes).\n",
                        NOTE_SIZE - 1);
                return -1;
            }
            used += (size_t)written;
        }
        ++number;
    }
}

static const char *total_seconds_error(const Config *config, long long *total)
{
    long long tail = config->minutes * 60 + config->seconds;

    if (config->hours > (MAX_SECONDS - tail) / 3600) {
        return "Duration is too large.";
    }
    *total = config->hours * 3600 + tail;
    if (*total == 0) {
        return "Total duration must be greater than zero.";
    }
    return NULL;
}

static int total_seconds(const Config *config, long long *total)
{
    const char *error = total_seconds_error(config, total);

    if (error != NULL) {
        fprintf(stderr, "%s\n", error);
        return -1;
    }
    return 0;
}

static int prompt_config(Config *config, long long *total)
{
    for (;;) {
        if (prompt_number("Hours", MAX_HOURS, &config->hours) == -1 ||
            prompt_number("Minutes", 59, &config->minutes) == -1 ||
            prompt_number("Seconds", 59, &config->seconds) == -1) {
            return -1;
        }
        if (total_seconds(config, total) == 0) {
            char input[64];

            for (;;) {
                puts(note_question);
                if (fgets(input, sizeof(input), stdin) == NULL) {
                    fputc('\n', stderr);
                    return -1;
                }
                if (strchr(input, '\n') == NULL) {
                    discard_line();
                } else {
                    input[strcspn(input, "\n")] = '\0';
                    if (strcmp(input, "y") == 0 || strcmp(input, "Y") == 0) {
                        return prompt_notes(config);
                    }
                    if (strcmp(input, "n") == 0 || strcmp(input, "N") == 0) {
                        return 0;
                    }
                }
                fprintf(stderr, "Please enter y or n.\n");
            }
        }
    }
}

static void draw_centered_message(int row, const char *text, int columns,
                                  attr_t attributes)
{
    int length = (int)strlen(text);
    int shown = length < columns ? length : columns;

    if (row < 0 || row >= LINES || shown <= 0) {
        return;
    }
    attron(attributes);
    mvaddnstr(row, (columns - shown) / 2, text, shown);
    attroff(attributes);
}

static void draw_text(int row, int column, const char *text, int columns)
{
    if (row >= 0 && row < LINES && column >= 0 && column < columns) {
        mvaddnstr(row, column, text, columns - column);
    }
}

static bool draw_setup(const Setup *setup)
{
    int rows;
    int columns;
    int start_y;
    int title_x;
    int form_x;
    int form_y;
    int note_y;
    int visible_note_lines;
    int completed_note_lines = setup->note_number - 1;
    int skipped_note_lines;
    int row;
    int index;
    int cursor_row = 0;
    int cursor_column = 0;

    getmaxyx(stdscr, rows, columns);
    erase();
    if (rows < SETUP_HEIGHT || columns < TITLE_WIDTH) {
        (void)curs_set(0);
        draw_centered_message(rows / 2 - 1, "Terminal too small.", columns,
                              A_BOLD);
        draw_centered_message(rows / 2, "Please resize the terminal.", columns,
                              A_NORMAL);
        refresh();
        return false;
    }

    (void)curs_set(1);
    start_y = (rows - SETUP_HEIGHT) / 2;
    title_x = (columns - TITLE_WIDTH) / 2;
    form_x = (columns - (int)(sizeof(note_heading) - 1)) / 2;
    form_y = start_y + TITLE_HEIGHT + 2;
    attron(A_BOLD);
    for (row = 0; row < TITLE_HEIGHT; ++row) {
        mvaddnstr(start_y + row, title_x, title[row], TITLE_WIDTH);
    }
    attroff(A_BOLD);

    for (index = 0; index < 3; ++index) {
        char prompt[64];
        const char *value = setup->field == index ? setup->input :
                            setup->entered[index];
        int prompt_length;
        size_t value_length = setup->field == index ? setup->input_length :
                              strlen(value);
        int available;
        size_t offset = 0;

        prompt_length = snprintf(prompt, sizeof(prompt),
                                 "%s [Default=%02lld]: ", number_labels[index],
                                 setup->defaults[index]);
        draw_text(form_y + index, form_x, prompt, columns);
        available = columns - form_x - prompt_length;
        if (available > 0) {
            if (value_length >= (size_t)available) {
                offset = value_length - (size_t)available + 1;
            }
            mvaddnstr(form_y + index, form_x + prompt_length,
                      value + offset, available - 1);
        }
        if (setup->field == index) {
            cursor_row = form_y + index;
            cursor_column = form_x + prompt_length +
                            (int)(value_length - offset);
        }
    }

    draw_text(form_y + 3, form_x, note_question, columns);
    if (setup->field == 3) {
        draw_text(form_y + 4, form_x, setup->input, columns);
        cursor_row = form_y + 4;
        cursor_column = form_x + (int)setup->input_length;
    } else if (setup->field == 4) {
        const char *line = setup->note;
        int total_note_lines = completed_note_lines + 1;
        char choice[] = {setup->note_choice, '\0'};

        draw_text(form_y + 4, form_x, choice, columns);
        draw_text(form_y + 5, form_x, note_heading, columns);
        note_y = form_y + 6;
        visible_note_lines = rows - note_y - 1;
        skipped_note_lines = total_note_lines > visible_note_lines ?
                             total_note_lines - visible_note_lines : 0;
        for (index = 0; index < skipped_note_lines; ++index) {
            line = strchr(line, '\n');
            if (line != NULL) {
                ++line;
            }
        }
        row = note_y;
        for (index = skipped_note_lines;
             index < completed_note_lines && line != NULL; ++index) {
            const char *end = strchr(line, '\n');
            size_t length = end == NULL ? strlen(line) : (size_t)(end - line);

            mvaddnstr(row++, form_x, line,
                      length < (size_t)(columns - form_x) ? (int)length :
                                                           columns - form_x);
            line = end == NULL ? NULL : end + 1;
        }
        {
            char prefix[32];
            int prefix_length = setup->note_blank ?
                                snprintf(prefix, sizeof(prefix), "   ") :
                                snprintf(prefix, sizeof(prefix), "%d. ",
                                         setup->note_number);
            int available = columns - form_x - prefix_length;
            size_t offset = 0;

            draw_text(row, form_x, prefix, columns);
            if (available > 0) {
                if (setup->input_length >= (size_t)available) {
                    offset = setup->input_length - (size_t)available + 1;
                }
                mvaddnstr(row, form_x + prefix_length,
                          setup->input + offset, available - 1);
            }
            cursor_row = row;
            cursor_column = form_x + prefix_length +
                            (int)(setup->input_length - offset);
        }
    }

    if (setup->message[0] != '\0') {
        draw_centered_message(rows - 1, setup->message, columns, A_BOLD);
    }
    move(cursor_row, cursor_column);
    refresh();
    return true;
}

static int read_setup_line(Setup *setup, char *input, size_t maximum)
{
    size_t length = 0;
    size_t overflow = 0;

    input[0] = '\0';
    for (;;) {
        int key;
        bool fits;

        setup->input = input;
        setup->input_length = length;
        fits = draw_setup(setup);
        key = getch();
        if (interrupted) {
            return -1;
        }
        if (key == ERR || key == KEY_RESIZE) {
            continue;
        }
        if (!fits) {
            continue;
        }
        if (key == '\n' || key == '\r' || key == KEY_ENTER) {
            return overflow == 0 ? 0 : 1;
        }
        if (key == KEY_BACKSPACE || key == 127 || key == '\b') {
            if (overflow > 0) {
                --overflow;
            } else if (length > 0) {
                input[--length] = '\0';
            }
        } else if (key == 21) {
            length = overflow = 0;
            input[0] = '\0';
        } else if (key >= ' ' && key <= UCHAR_MAX && key != 127) {
            if (length < maximum) {
                input[length++] = (char)key;
                input[length] = '\0';
            } else {
                ++overflow;
            }
        }
    }
}

static int prompt_config_screen(Config *config, long long *total)
{
    static const long long maximums[] = {MAX_HOURS, 59, 59};
    long long *values[] = {&config->hours, &config->minutes, &config->seconds};
    Setup setup = {.note_number = 1};
    int index;

    timeout(100);
    for (;;) {
        setup.defaults[0] = config->hours;
        setup.defaults[1] = config->minutes;
        setup.defaults[2] = config->seconds;
        memset(setup.entered, 0, sizeof(setup.entered));

        for (index = 0; index < 3; ++index) {
            char input[64];

            setup.field = index;
            for (;;) {
                long long parsed;
                int input_result = read_setup_line(&setup, input,
                                                   sizeof(input) - 2);

                if (input_result == -1) {
                    return -1;
                }
                if (input_result == 1) {
                    (void)snprintf(setup.message, sizeof(setup.message),
                                   "Input is too long.");
                    continue;
                }
                if (input[0] == '\0') {
                    setup.message[0] = '\0';
                    break;
                }
                if (parse_number_value(input, maximums[index], &parsed)) {
                    *values[index] = parsed;
                    memcpy(setup.entered[index], input, strlen(input) + 1);
                    setup.message[0] = '\0';
                    break;
                }
                (void)snprintf(setup.message, sizeof(setup.message),
                               "Invalid %s '%s': expected 0-%lld.",
                               number_labels[index], input, maximums[index]);
            }
        }
        {
            const char *error = total_seconds_error(config, total);

            if (error == NULL) {
                break;
            }
            (void)snprintf(setup.message, sizeof(setup.message), "%s", error);
        }
    }

    setup.field = 3;
    for (;;) {
        char input[2];
        int input_result = read_setup_line(&setup, input, sizeof(input) - 1);

        if (input_result == -1) {
            return -1;
        }
        if (input_result == 0 && (input[0] == 'y' || input[0] == 'Y') &&
            input[1] == '\0') {
            setup.note_choice = input[0];
            setup.message[0] = '\0';
            break;
        }
        if (input_result == 0 && (input[0] == 'n' || input[0] == 'N') &&
            input[1] == '\0') {
            goto done;
        }
        (void)snprintf(setup.message, sizeof(setup.message),
                       "Please enter y or n.");
    }

    setup.field = 4;
    for (;;) {
        char input[NOTE_LINE_SIZE];
        int input_result = read_setup_line(&setup, input,
                                           sizeof(input) - 2);

        if (input_result == -1) {
            return -1;
        }
        if (input_result == 1) {
            (void)snprintf(setup.message, sizeof(setup.message),
                           "Note line is too long (maximum %d bytes).",
                           NOTE_LINE_SIZE - 2);
            continue;
        }
        if (input[0] == '\0') {
            if (setup.note_blank) {
                memcpy(config->note, setup.note, strlen(setup.note) + 1);
                break;
            }
            setup.note_blank = true;
            continue;
        }

        setup.note_blank = false;
        {
            size_t used = strlen(setup.note);
            int written = snprintf(setup.note + used, NOTE_SIZE - used,
                                   "%s%d. %s", used == 0 ? "" : "\n",
                                   setup.note_number, input);

            if (written < 0 || (size_t)written >= NOTE_SIZE - used) {
                return -2;
            }
        }
        setup.message[0] = '\0';
        ++setup.note_number;
    }

done:
    nodelay(stdscr, TRUE);
    (void)curs_set(0);
    return 0;
}

static int install_signal_handlers(void)
{
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    action.sa_handler = handle_signal;
    sigemptyset(&action.sa_mask);
    return sigaction(SIGINT, &action, NULL) == -1 ||
           sigaction(SIGTERM, &action, NULL) == -1 ? -1 : 0;
}

int main(int argc, char **argv)
{
    Config config = {0, 0, 0, ""};
    struct timespec clock_check;
    long long duration;
    Timer timer;
    bool quit = false;
    bool bell_rung = false;
    bool display_ready = false;
    int result;

    result = parse_arguments(argc, argv, &config);
    if (result != 0) {
        return result > 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (argc != 1 && total_seconds(&config, &duration) == -1) {
        return EXIT_FAILURE;
    }
    if (clock_gettime(CLOCK_MONOTONIC, &clock_check) == -1) {
        perror("clock_gettime");
        return EXIT_FAILURE;
    }
    if (install_signal_handlers() == -1) {
        perror("sigaction");
        return EXIT_FAILURE;
    }
    if (argc == 1 && isatty(STDIN_FILENO) && isatty(STDOUT_FILENO)) {
        if (display_init() == -1) {
            fprintf(stderr, "Could not initialize ncurses.\n");
            return EXIT_FAILURE;
        }
        display_ready = true;
        result = prompt_config_screen(&config, &duration);
        if (result != 0) {
            display_shutdown();
            if (result == -2) {
                fprintf(stderr, "Notes are too long (maximum %d bytes).\n",
                        NOTE_SIZE - 1);
            }
            return EXIT_FAILURE;
        }
    } else if (argc == 1 && prompt_config(&config, &duration) == -1) {
        return EXIT_FAILURE;
    }
    if (!display_ready && display_init() == -1) {
        fprintf(stderr, "Could not initialize ncurses.\n");
        return EXIT_FAILURE;
    }

    timer_init(&timer, duration);
    while (!quit && !interrupted) {
        int key;

        display_draw(&timer, config.note);
        if (timer.finished && !bell_rung) {
            beep();
            bell_rung = true;
        }

        while ((key = getch()) != ERR) {
            switch (key) {
            case ' ':
            case 'p':
            case 'P':
                timer_toggle_pause(&timer);
                break;
            case 'r':
            case 'R':
                timer_reset(&timer);
                bell_rung = false;
                break;
            case 'e':
            case 'E':
                display_shutdown();
                printf("Current note:\n%s\n\nThe timer is still running.\n",
                       config.note[0] == '\0' ? "(none)" : config.note);
                if (prompt_notes(&config) == -1) {
                    return EXIT_FAILURE;
                }
                if (display_init() == -1) {
                    fprintf(stderr, "Could not reinitialize ncurses.\n");
                    return EXIT_FAILURE;
                }
                break;
            case 'q':
            case 'Q':
                quit = true;
                break;
            default:
                break;
            }
        }
        napms(50);
    }

    display_shutdown();
    return EXIT_SUCCESS;
}
