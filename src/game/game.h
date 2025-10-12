#pragma once

#include "render_api.h"
#include "world.h"

// called once at game launch
void game_init(WorldState *world);

// called when input is received (like key presses). will drive forward the turn
// based simulation
void game_input(WorldState *world, InputCommand command);

// run once per frame - advances animations, turn queue, and internal tick
// system
void game_frame(WorldState *world, double dt);

// render the game state using the provided render context
void game_render(WorldState *world, RenderContext *ctx);
