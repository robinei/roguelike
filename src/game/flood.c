#include "flood.h"
#include "random.h"

#define MAX_DEPTH 255
#define BOUNDARY_DEPTH 199

#define IX(x, y) ((y) * MAP_WIDTH_MAX + (x))

// Temporary buffer for double-buffering
static uint8_t temp_depth[MAP_WIDTH_MAX * MAP_HEIGHT_MAX];

// Helper to check if a cell is solid (impassable)
static inline bool is_solid(Map *map, int x, int y) {
  return !map->cells[IX(x, y)].passable;
}

void flood_simulate_step(Map *map) {
  int width = map->width;
  int height = map->height;

  // Set boundary cells to maximum depth
  for (int x = 0; x < width; x++) {
    if (!is_solid(map, x, 0)) {
      temp_depth[IX(x, 0)] = BOUNDARY_DEPTH;
    }
    if (!is_solid(map, x, height - 1)) {
      temp_depth[IX(x, height - 1)] = BOUNDARY_DEPTH;
    }
  }
  for (int y = 0; y < height; y++) {
    if (!is_solid(map, 0, y)) {
      temp_depth[IX(0, y)] = BOUNDARY_DEPTH;
    }
    if (!is_solid(map, width - 1, y)) {
      temp_depth[IX(width - 1, y)] = BOUNDARY_DEPTH;
    }
  }

  // Cellular automata: each cell tries to equalize with neighbors
  for (int y = 1; y < height - 1; y++) {
    for (int x = 1; x < width - 1; x++) {
      int idx = IX(x, y);

      // Skip solid cells
      if (is_solid(map, x, y)) {
        temp_depth[idx] = 0;
        continue;
      }

      int current = map->water_depth[idx];

      // Find maximum neighbor depth (water tries to reach highest neighbor
      // level)
      int max_neighbor = 0;

      // Check 4 cardinal neighbors
      if (x > 0 && !is_solid(map, x - 1, y)) {
        int neighbor = map->water_depth[IX(x - 1, y)];
        if (neighbor > max_neighbor)
          max_neighbor = neighbor;
      }
      if (x < width - 1 && !is_solid(map, x + 1, y)) {
        int neighbor = map->water_depth[IX(x + 1, y)];
        if (neighbor > max_neighbor)
          max_neighbor = neighbor;
      }
      if (y > 0 && !is_solid(map, x, y - 1)) {
        int neighbor = map->water_depth[IX(x, y - 1)];
        if (neighbor > max_neighbor)
          max_neighbor = neighbor;
      }
      if (y < height - 1 && !is_solid(map, x, y + 1)) {
        int neighbor = map->water_depth[IX(x, y + 1)];
        if (neighbor > max_neighbor)
          max_neighbor = neighbor;
      }

      // Flow rate: add a fraction of the difference to max neighbor
      // Water must pool to a minimum depth before it can flow to neighbors
      const int min_source_depth = 50; // Minimum depth needed to be a source
                                       // (higher = steeper wavefront)
      if (current >= max_neighbor || max_neighbor < min_source_depth) {
        temp_depth[idx] = current;
        continue;
      }

      int diff = max_neighbor - current;
      int flow = diff / 2; // Take 50% of the difference each step (higher =
                           // steeper wavefront)
      if (flow < 1)
        flow = 1; // Minimum flow to prevent complete stalling

      // Add noise to break up uniform wavefronts
      int noise_range = (flow / 2) + 6;
      int noise = (random64() % (noise_range * 2)) - noise_range;
      flow += noise;

      // Clamp to valid range
      if (flow < 1)
        flow = 1;
      int new_depth = current + flow;

      // Cap at maximum neighbor - prevents exceeding boundary depth
      // transitively
      if (new_depth > max_neighbor)
        new_depth = max_neighbor;
      if (new_depth > MAX_DEPTH)
        new_depth = MAX_DEPTH;

      temp_depth[idx] = new_depth;
    }
  }

  // Copy back
  memcpy(map->water_depth, temp_depth, sizeof(temp_depth));
}
