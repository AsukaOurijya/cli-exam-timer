#define _POSIX_C_SOURCE 200809L

#include "timer.h"

#include <assert.h>
#include <time.h>

static void wait_ms(long milliseconds)
{
    struct timespec delay = {milliseconds / 1000,
                             milliseconds % 1000 * 1000000L};

    while (nanosleep(&delay, &delay) == -1) {
    }
}

int main(void)
{
    Timer timer;
    long long paused_at;

    timer_init(&timer, 1);
    assert(timer_remaining_seconds(&timer) == 1);
    timer_toggle_pause(&timer);
    paused_at = timer.remaining_ns;
    wait_ms(100);
    timer_update(&timer);
    assert(timer.paused && timer.remaining_ns == paused_at);

    timer_toggle_pause(&timer);
    wait_ms(1100);
    assert(timer_remaining_seconds(&timer) == 0);
    assert(timer.finished);

    timer_reset(&timer);
    assert(!timer.paused && !timer.finished);
    assert(timer_remaining_seconds(&timer) == 1);
    return 0;
}
