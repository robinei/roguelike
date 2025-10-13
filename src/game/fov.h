#pragma once

#include "map.h"

// Spiral Path FOV algorithm
// Adapted from: http://www.roguebasin.com/index.php?title=Spiral_Path_FOV

#define PLAYER_FOV_RADIUS 20
#define PLAYER_TORCH_RADIUS 20

// Compute field of view from origin position
// Sets the visible flag on all tiles within line of sight
// Uses the passable flag to determine blocking (non-passable blocks sight)
void fov_compute(Map *map, int origin_x, int origin_y, int radius);
