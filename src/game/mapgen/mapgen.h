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

// Generate BSP dungeon in the entire map (with border)
void mapgen_bsp(Map *map, const BSPGenParams *params);

// Generate BSP dungeon in a specific rectangular region of the map
// Useful for chunk-based generation or composing multiple generators
void mapgen_bsp_region(Map *map, int region_x, int region_y,
                       int region_width, int region_height,
                       const BSPGenParams *params);

typedef struct {
  int iterations;        // Number of refinement iterations
  int attempts_per_tile; // Number of random attempts per tile
} CSPGenParams;

// Generate terrain using Constraint Satisfaction with Local Minimum Conflicts
void mapgen_csp(Map *map, const CSPGenParams *params);

// Generate CSP terrain in a specific rectangular region of the map
void mapgen_csp_region(Map *map, int region_x, int region_y,
                       int region_width, int region_height,
                       const CSPGenParams *params);