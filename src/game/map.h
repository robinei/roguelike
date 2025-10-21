#pragma once

#include "common.h"
#include <stdbool.h>

typedef struct {
  uint32_t passable : 1;
  uint32_t visible : 1;
  uint32_t tile : 14;
  uint32_t category : 4;
} MapCell;

typedef struct {
  int width;
  int height;
  MapCell cells[MAP_WIDTH_MAX * MAP_HEIGHT_MAX];
  uint8_t water_depth[MAP_WIDTH_MAX * MAP_HEIGHT_MAX];
} Map;

typedef struct {
  bool generated;
} MapChunk;

typedef struct {
  // current (center) chunk
  int curr_chunk_x;
  int curr_chunk_y;
  MapChunk chunks[MAP_CHUNK_TOTAL_X * MAP_CHUNK_TOTAL_Y];
} WorldMap;

// Get a random passable position on the map
// Returns false if no passable position found after max_attempts
bool map_get_random_passable(Map *map, int region_x, int region_y,
                             int region_width, int region_height,
                             Position *out_pos, int max_attempts);
