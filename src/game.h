#pragma once

#include <stdint.h>

// the game expects a tick 10 times per second. the passed tick value is the
// current tick count (always increasing)
void game_tick(uint64_t tick);

// run once per frame
void game_frame(double dt);
