#ifndef INC_SIMPLE_TIMER_H
#define INC_SIMPLE_TIMER_H
#include "common-defines.h"

typedef struct simple_timer_t {
    uint64_t wait_time;     // Time interval
    uint64_t target_time;   // The number of system ticks that have passed
    bool auto_reset;        // As soon as we check the timer to see if it elapsed, it'll set itself again and will be ready to be checked again
    bool has_elapsed;
}simple_timer_t;

void simple_timer_setup(simple_timer_t* timer, uint64_t wait_time, bool auto_reset);
bool simple_timer_has_elapsed(simple_timer_t* timer);
void simple_timer_reset(simple_timer_t* timer);


#endif // INC_SIMPLE_TIMER_H