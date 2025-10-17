#pragma once

#include "../common.h"

#define ASTAR_PATH_MAX_LENGTH 1024
#define ASTAR_COST_INFINITE INT32_MAX

/*
  Returns the cost of a single move from (sx,sy) to (tx,ty) which will be
  adjacent (up, down, left, right).

  Costs can be negative to incentivize tactical detours:
    - Positive: normal movement cost (e.g., 10 for regular tile)
    - Negative: bonus/reward (e.g., -50 for picking up health)
    - ASTAR_COST_INFINITE: impassable

  Example: NPC pathfinding to player might give:
    - Normal tile: 10
    - Health pickup: -40 (net cost of -30 for that tile)
    - Cover position: -20 (slight preference)
    - Impassable wall: ASTAR_COST_INFINITE
*/
typedef int32_t (*AStarCostFunction)(void *ctx, int sx, int sy, int tx, int ty);

/*
  Finds path from (sx,sy) to (tx,ty), using the specified cost function.
  Resulting path is stored into the moves buffer passed into this function.
  The moves buffer must be at least ASTAR_PATH_MAX_LENGTH long.

  The coordinates are assumed to be within the the passed-in map_width and
  map_height (which must also be within MAP_WIDTH_MAX and MAP_HEIGHT_MAX).

  Return value: number of moves, or -1 if path not found.
*/
int astar_find_path(void *ctx, AStarCostFunction cost_func, int map_width,
                    int map_height, int sx, int sy, int tx, int ty,
                    Direction *moves_out);
