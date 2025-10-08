#pragma once

#include "world.h"
#include <stdint.h>

// called once at game launch
void game_init(WorldState *world);

// the game expects a tick 10 times per second. the passed tick value is the
// current tick count (always increasing)
void game_tick(WorldState *world, uint64_t tick);

// run once per frame
void game_frame(WorldState *world, double dt);

typedef enum {
  INPUT_CMD_UP,
  INPUT_CMD_UP_RIGHT,
  INPUT_CMD_RIGHT,
  INPUT_CMD_DOWN_RIGHT,
  INPUT_CMD_DOWN,
  INPUT_CMD_DOWN_LEFT,
  INPUT_CMD_LEFT,
  INPUT_CMD_UP_LEFT,

  INPUT_CMD_PERIOD,
} InputCommand;

// called when input is received (like key presses). will drive forward the turn
// based simulation
void game_input(WorldState *world, InputCommand command);
