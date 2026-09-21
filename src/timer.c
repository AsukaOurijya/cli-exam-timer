#define _POSIX_C_SOURCE 200809L

#include "timer.h"

#include <time.h>

#define NS_PER_SECOND INT64_C(1000000000)

static int64_t monotonic_ns(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) == -1) {
        return 0;
    }
    return (int64_t)now.tv_sec * NS_PER_SECOND + now.tv_nsec;
}

void timer_init(Timer *timer, long long seconds)
{
    timer->initial_ns = (int64_t)seconds * NS_PER_SECOND;
    timer_reset(timer);
}

void timer_update(Timer *timer)
{
    if (timer->paused || timer->finished) {
        return;
    }

    timer->remaining_ns = timer->deadline_ns - monotonic_ns();
    if (timer->remaining_ns <= 0) {
        timer->remaining_ns = 0;
        timer->finished = true;
    }
}

long long timer_remaining_seconds(Timer *timer)
{
    timer_update(timer);
    return (long long)((timer->remaining_ns + NS_PER_SECOND - 1) /
                       NS_PER_SECOND);
}

void timer_toggle_pause(Timer *timer)
{
    if (timer->finished) {
        return;
    }

    if (timer->paused) {
        timer->deadline_ns = monotonic_ns() + timer->remaining_ns;
        timer->paused = false;
    } else {
        timer_update(timer);
        if (!timer->finished) {
            timer->paused = true;
        }
    }
}

void timer_reset(Timer *timer)
{
    timer->remaining_ns = timer->initial_ns;
    timer->deadline_ns = monotonic_ns() + timer->remaining_ns;
    timer->paused = false;
    timer->finished = false;
}
