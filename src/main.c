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

#define NOTE_SIZE 4096
#define NOTE_LINE_SIZE 512
#define MAX_SECONDS (INT64_MAX / INT64_C(1000000000))
#define MAX_HOURS (MAX_SECONDS / 3600)

typedef struct {
    long long hours;
    long long minutes;
    long long seconds;
    char note[NOTE_SIZE];
} Config;

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

static int parse_number(const char *text, long long maximum,
                        const char *name, long long *value)
{
    char *end;
    long long parsed;

    errno = 0;
    parsed = strtoll(text, &end, 10);
    if (errno != 0 || text == end || *end != '\0' || parsed < 0 ||
        parsed > maximum) {
        fprintf(stderr, "Invalid %s '%s': expected 0-%lld.\n",
                name, text, maximum);
        return -1;
    }
    *value = parsed;
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
    static const char heading[] =
        "Note [Click enter to new line, click enter twice to proceed to timer]:";
    char input[NOTE_LINE_SIZE];
    size_t used = 0;
    int number = 1;
    bool blank = false;

    puts(heading);
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
                return 0;
            }
            blank = true;
            continue;
        }

        blank = false;
        {
            int written = snprintf(config->note + used, NOTE_SIZE - used,
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

static int total_seconds(const Config *config, long long *total)
{
    long long tail = config->minutes * 60 + config->seconds;

    if (config->hours > (MAX_SECONDS - tail) / 3600) {
        fprintf(stderr, "Duration is too large.\n");
        return -1;
    }
    *total = config->hours * 3600 + tail;
    if (*total == 0) {
        fprintf(stderr, "Total duration must be greater than zero.\n");
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
            return prompt_notes(config);
        }
    }
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
    int result;

    result = parse_arguments(argc, argv, &config);
    if (result != 0) {
        return result > 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (argc == 1 ? prompt_config(&config, &duration) == -1 :
                    total_seconds(&config, &duration) == -1) {
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
    if (display_init() == -1) {
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
