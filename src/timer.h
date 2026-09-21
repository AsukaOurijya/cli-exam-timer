#ifndef TIMER_H
#define TIMER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int64_t initial_ns;
    int64_t remaining_ns;
    int64_t deadline_ns;
    bool paused;
    bool finished;
} Timer;

void timer_init(Timer *timer, long long seconds);
void timer_update(Timer *timer);
long long timer_remaining_seconds(Timer *timer);
void timer_toggle_pause(Timer *timer);
void timer_reset(Timer *timer);

#endif
