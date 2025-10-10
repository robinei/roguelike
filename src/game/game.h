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

// render the game state using the provided platform context
void game_render(WorldState *world, PlatformContext *platform);

#ifdef __wasm__
// WASM-friendly render function that takes viewport dimensions directly
// and uses the imported execute_render_commands function
void game_render_wasm(WorldState *world, int viewport_width_px,
                      int viewport_height_px, int tile_size);
#endif
