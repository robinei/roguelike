#include "map.h"
#include "mapgen/mapgen.h"
#include "random.h"
#include "render_api.h"

bool map_get_random_passable(Map *map, int region_x, int region_y,
                             int region_width, int region_height,
                             Position *out_pos, int max_attempts) {
  for (int attempt = 0; attempt < max_attempts; attempt++) {
    int x = region_x + random64() % region_width;
    int y = region_y + random64() % region_height;

    if (map->cells[y * MAP_WIDTH_MAX + x].passable) {
      out_pos->x = x;
      out_pos->y = y;
      return true;
    }
  }

  return false;
}

// Generate a single chunk with boundary constraints from neighbors
// chunk_x, chunk_y are local window coordinates (0-2)
static void generate_chunk(int chunk_x, int chunk_y) {
  assert(chunk_x >= 0 && chunk_x < MAP_CHUNK_WINDOW_X);
  assert(chunk_y >= 0 && chunk_y < MAP_CHUNK_WINDOW_Y);

  // Calculate world chunk coordinates
  int world_chunk_x = WORLD.worldmap.curr_chunk_x + (chunk_x - 1);
  int world_chunk_y = WORLD.worldmap.curr_chunk_y + (chunk_y - 1);
  assert(world_chunk_x >= 0 && world_chunk_x < MAP_CHUNK_TOTAL_X);
  assert(world_chunk_y >= 0 && world_chunk_y < MAP_CHUNK_TOTAL_X);

  int world_chunk_idx = world_chunk_y * MAP_CHUNK_TOTAL_X + world_chunk_x;
  if (WORLD.worldmap.chunks[world_chunk_idx].generated) {
    return; // Already generated
  }

  CSPGenParams csp_params = {
      .iterations = 100000,
      .attempts_per_tile = 5,
  };

  int region_x = chunk_x * MAP_CHUNK_WIDTH;
  int region_y = chunk_y * MAP_CHUNK_HEIGHT;

  mapgen_csp_region(&WORLD.map, region_x, region_y, MAP_CHUNK_WIDTH,
                    MAP_CHUNK_HEIGHT, &csp_params);

  WORLD.worldmap.chunks[world_chunk_idx].generated = true;
}

// Shift the map window by (dx, dy) chunks when player crosses chunk boundary
// Uses in-place shifting: iterate in the right direction to avoid overwriting
static void shift_map_window(int dx, int dy) {
  // Mark outgoing chunks as not generated (so they can be regenerated later)
  for (int cy = 0; cy < MAP_CHUNK_WINDOW_Y; cy++) {
    for (int cx = 0; cx < MAP_CHUNK_WINDOW_X; cx++) {
      // Calculate where this chunk will end up after the shift
      // Note: chunks shift opposite to player movement direction
      int dest_cx = cx - dx;
      int dest_cy = cy - dy;

      // If it shifts out of the window, mark as not generated in world map
      if (dest_cx < 0 || dest_cx >= MAP_CHUNK_WINDOW_X || dest_cy < 0 ||
          dest_cy >= MAP_CHUNK_WINDOW_Y) {
        // Calculate world chunk coordinates (before the shift)
        int world_chunk_x = WORLD.worldmap.curr_chunk_x + (cx - 1);
        int world_chunk_y = WORLD.worldmap.curr_chunk_y + (cy - 1);

        if (world_chunk_x >= 0 && world_chunk_x < MAP_CHUNK_TOTAL_X &&
            world_chunk_y >= 0 && world_chunk_y < MAP_CHUNK_TOTAL_Y) {
          int world_chunk_idx =
              world_chunk_y * MAP_CHUNK_TOTAL_X + world_chunk_x;
          WORLD.worldmap.chunks[world_chunk_idx].generated = false;
        }
      }
    }
  }

  // Calculate shift in tiles
  int shift_x = -dx * MAP_CHUNK_WIDTH;
  int shift_y = -dy * MAP_CHUNK_HEIGHT;

  // Determine iteration direction to avoid overwriting source data
  int y_start = (shift_y > 0) ? MAP_HEIGHT_MAX - 1 : 0;
  int y_end = (shift_y > 0) ? -1 : MAP_HEIGHT_MAX;
  int y_step = (shift_y > 0) ? -1 : 1;

  int x_start = (shift_x > 0) ? MAP_WIDTH_MAX - 1 : 0;
  int x_end = (shift_x > 0) ? -1 : MAP_WIDTH_MAX;
  int x_step = (shift_x > 0) ? -1 : 1;

  // Shift map data in-place
  for (int y = y_start; y != y_end; y += y_step) {
    for (int x = x_start; x != x_end; x += x_step) {
      int src_x = x - shift_x;
      int src_y = y - shift_y;

      // Check if both source AND destination are within bounds
      if (x >= 0 && x < MAP_WIDTH_MAX && y >= 0 && y < MAP_HEIGHT_MAX &&
          src_x >= 0 && src_x < MAP_WIDTH_MAX && src_y >= 0 &&
          src_y < MAP_HEIGHT_MAX) {
        // Copy from old position
        WORLD.map.cells[y * MAP_WIDTH_MAX + x] =
            WORLD.map.cells[src_y * MAP_WIDTH_MAX + src_x];
        WORLD.map.water_depth[y * MAP_WIDTH_MAX + x] =
            WORLD.map.water_depth[src_y * MAP_WIDTH_MAX + src_x];
      } else if (x >= 0 && x < MAP_WIDTH_MAX && y >= 0 && y < MAP_HEIGHT_MAX) {
        // Destination in bounds, source out of bounds - initialize to default
        WORLD.map.cells[y * MAP_WIDTH_MAX + x] = (MapCell){
            .passable = 1,
            .visible = 0,
            .tile = TILE_NONE,
            .category = 0,
        };
        WORLD.map.water_depth[y * MAP_WIDTH_MAX + x] = 0;
      }
    }
  }

  // Shift all entity positions and remove entities that are now outside bounds
  WORLD_QUERY(i, BITS(Position)) {
    Position *pos = &PART(Position, i);
    pos->x += shift_x;
    pos->y += shift_y;

    // Check if entity is now outside the map bounds
    if (pos->x < 0 || pos->x >= MAP_WIDTH_MAX || pos->y < 0 ||
        pos->y >= MAP_HEIGHT_MAX) {
      // Entity has been shifted out of the active window
      // Don't despawn the player! Just clamp their position
      if (entity_is_player(i)) {
        if (pos->x < 0)
          pos->x = 0;
        if (pos->x >= MAP_WIDTH_MAX)
          pos->x = MAP_WIDTH_MAX - 1;
        if (pos->y < 0)
          pos->y = 0;
        if (pos->y >= MAP_HEIGHT_MAX)
          pos->y = MAP_HEIGHT_MAX - 1;
      } else {
        // Remove non-player entities that are out of bounds
        entity_free(i);
      }
    }
  }
}

// Check if player entered a new chunk and generate neighbors if needed
void ensure_chunks_around_position(int player_x, int player_y) {
  // Calculate which chunk the player is in (within the 3x3 window)
  int player_chunk_x = player_x / MAP_CHUNK_WIDTH;  // 0, 1, or 2
  int player_chunk_y = player_y / MAP_CHUNK_HEIGHT; // 0, 1, or 2
  assert(player_chunk_x >= 0 && player_chunk_x <= MAP_CHUNK_WINDOW_X);
  assert(player_chunk_y >= 0 && player_chunk_y <= MAP_CHUNK_WINDOW_Y);

  // Calculate offset from center chunk
  int dx = player_chunk_x - MAP_CHUNK_WINDOW_X / 2; // -1, 0, or +1
  int dy = player_chunk_y - MAP_CHUNK_WINDOW_Y / 2; // -1, 0, or +1
  assert(dx * dx <= 1);
  assert(dy * dy <= 1);

  if (dx != 0 || dy != 0) {
    // Player crossed chunk boundary - shift the window
    shift_map_window(dx, dy);

    // Update world chunk coordinates
    WORLD.worldmap.curr_chunk_x += dx;
    WORLD.worldmap.curr_chunk_y += dy;
  }

  // Generate only the chunk the player is standing on (center chunk)
  // Later we will make this ensure the 3x3 around the player is generated,
  // but for now only the single chunk so we can see them being generated
  // as the player walks onto them
  for (int chunk_y = 0; chunk_y < MAP_CHUNK_WINDOW_Y; chunk_y++) {
    for (int chunk_x = 0; chunk_x < MAP_CHUNK_WINDOW_X; chunk_x++) {
      generate_chunk(chunk_x, chunk_y);
    }
  }
}