#pragma once

#include "common.h"

typedef struct {
  uint16_t passable : 1;
  uint16_t visible : 1;
  uint16_t tile : 14;
} MapCell;

typedef struct {
  int width;
  int height;
  MapCell cells[MAP_WIDTH_MAX * MAP_HEIGHT_MAX];
  uint8_t water_depth[MAP_WIDTH_MAX * MAP_HEIGHT_MAX];
} Map;

// Get a random passable position on the map
// Returns false if no passable position found after max_attempts
bool map_get_random_passable(Map *map, Position *out_pos, int max_attempts);
