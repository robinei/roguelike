#pragma once

#include "../map.h"

typedef struct {
  int max_depth;              // Maximum BSP tree depth
  int min_region_size;        // Stop splitting if region is smaller than this
  int min_child_size;         // Minimum size for each child after split
  int split_threshold;        // Prefer splitting along longer axis if above this
  int min_room_size;          // Minimum room dimensions
  int room_padding;           // Padding around rooms within regions
  int map_border;             // Border around the entire map edge
} BSPGenParams;

void mapgen_bsp(Map *map, const BSPGenParams *params);