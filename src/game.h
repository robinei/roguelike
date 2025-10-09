#pragma once

#include "render_api.h"
#include "world.h"

// called once at game launch
void game_init(WorldState *world);

// the game expects a tick 10 times per second. the passed tick value is the
// current tick count (always increasing)
void game_tick(WorldState *world, uint64_t tick);

// run once per frame
void game_frame(WorldState *world, double dt);

// render the game state using the provided platform context
void game_render(WorldState *world, PlatformContext *platform);

// called when input is received (like key presses). will drive forward the turn
// based simulation
void game_input(WorldState *world, InputCommand command);
