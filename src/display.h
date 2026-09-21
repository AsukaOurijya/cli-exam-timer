#ifndef DISPLAY_H
#define DISPLAY_H

#include "timer.h"

int display_init(void);
void display_draw(Timer *timer, const char *note);
void display_shutdown(void);

#endif
