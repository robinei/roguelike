#include "map.h"
#include "api.h"
#include "mapgen/mapgen.h"
#include "random.h"
#include "render_api.h"
#include "utils/bbuf.h"
#include "utils/sdefl.h"
#include "utils/sinfl.h"
#include "world.h"
#include <stdint.h>
#include <string.h>

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
void generate_chunk(int chunk_x, int chunk_y) {
  assert(chunk_x >= 0 && chunk_x < MAP_CHUNK_WINDOW_X);
  assert(chunk_y >= 0 && chunk_y < MAP_CHUNK_WINDOW_Y);

  // Calculate world chunk coordinates
  int world_chunk_x = WORLD.worldmap.curr_chunk_x + (chunk_x - 1);
  int world_chunk_y = WORLD.worldmap.curr_chunk_y + (chunk_y - 1);
  assert(world_chunk_x >= 0 && world_chunk_x < MAP_CHUNK_TOTAL_X);
  assert(world_chunk_y >= 0 && world_chunk_y < MAP_CHUNK_TOTAL_X);

  int world_chunk_idx = world_chunk_y * MAP_CHUNK_TOTAL_X + world_chunk_x;

  // Don't regenerate if already loaded or currently loading
  ChunkState state = WORLD.worldmap.chunks[world_chunk_idx].state;
  if (state == CHUNK_LOADED || state == CHUNK_LOADING) {
    return;
  }

  CSPGenParams csp_params = {
      .iterations = 100000,
      .attempts_per_tile = 5,
  };

  int region_x = chunk_x * MAP_CHUNK_WIDTH;
  int region_y = chunk_y * MAP_CHUNK_HEIGHT;

  mapgen_csp_region(&WORLD.map, region_x, region_y, MAP_CHUNK_WIDTH,
                    MAP_CHUNK_HEIGHT, &csp_params);

  WORLD.worldmap.chunks[world_chunk_idx].state = CHUNK_LOADED;
  output_message("Generated chunk (%d, %d)", world_chunk_x, world_chunk_y);
}

static uint64_t calc_chunk_key(int world_chunk_x, int world_chunk_y) {
  // Pack chunk coordinates into a 64-bit key
  // Upper 32 bits: world_chunk_y, Lower 32 bits: world_chunk_x
  return ((uint64_t)world_chunk_y << 32) | (uint64_t)world_chunk_x;
}

// Serialize a chunk's map data and entities into a buffer
// Populates out_entities with all entities that were serialized (for freeing)
static void serialize_chunk(int chunk_x, int chunk_y, ByteBuffer *buf,
                            EntitySet *out_entities) {
  int region_x = chunk_x * MAP_CHUNK_WIDTH;
  int region_y = chunk_y * MAP_CHUNK_HEIGHT;

  // Pack chunk format version (for future compatibility)
  bbuf_pack_u32(buf, 1, "chunk_version");

  // Pack map cells for this chunk region (row by row)
  for (int y = 0; y < MAP_CHUNK_HEIGHT; y++) {
    int map_y = region_y + y;
    int start_idx = map_y * MAP_WIDTH_MAX + region_x;

    // Copy entire row of cells and water depth
    bbuf_pack_bytes(buf, &WORLD.map.cells[start_idx],
                    MAP_CHUNK_WIDTH * sizeof(MapCell), "map_cells_row");
    bbuf_pack_bytes(buf, &WORLD.map.water_depth[start_idx], MAP_CHUNK_WIDTH,
                    "water_depth_row");
  }

  // Collect all entities in this chunk region
  *out_entities = (EntitySet){0};

  WORLD_QUERY(i, BITS(Position)) {
    Position *pos = &PART(Position, i);
    if (pos->x >= region_x && pos->x < region_x + MAP_CHUNK_WIDTH &&
        pos->y >= region_y && pos->y < region_y + MAP_CHUNK_HEIGHT) {
      entityset_add(out_entities, i);
    }
  }

  // Expand to include all descendants (inventory/equipment)
  entityset_expand_descendants(out_entities);

  // Pack entity count
  bbuf_pack_u32(buf, out_entities->count, "entity_count");

  // Pack each entity with its original index (for parent remapping)
  for (uint32_t i = 0; i < out_entities->count; i++) {
    EntityIndex entity = out_entities->entities[i];
    bbuf_pack_u16(buf, entity, "entity_old_index");
    entity_pack(entity, buf);
  }
}

// Deserialize a chunk's map data and entities from a buffer
void deserialize_chunk(int chunk_x, int chunk_y, const void *data,
                       size_t data_size) {
  // Decompress the data first
  static uint8_t decompressed_buffer[512 * 1024];
  int decompressed_size =
      sinflate(decompressed_buffer, (const uint8_t *)data, data_size);
  assert(decompressed_size > 0 && "Decompression failed");
  assert(decompressed_size <= (int)sizeof(decompressed_buffer));

  ByteBuffer buf = {
      .size = decompressed_size,
      .read_pos = 0,
      .capacity = decompressed_size,
      .data = decompressed_buffer,
  };

  // Unpack version
  uint32_t version = bbuf_unpack_u32(&buf, "chunk_version");
  (void)version; // Unused for now, but could check compatibility

  int region_x = chunk_x * MAP_CHUNK_WIDTH;
  int region_y = chunk_y * MAP_CHUNK_HEIGHT;

  // Unpack map cells (row by row)
  for (int y = 0; y < MAP_CHUNK_HEIGHT; y++) {
    int map_y = region_y + y;
    int start_idx = map_y * MAP_WIDTH_MAX + region_x;

    bbuf_unpack_bytes(&buf, &WORLD.map.cells[start_idx],
                      MAP_CHUNK_WIDTH * sizeof(MapCell), "map_cells_row");
    bbuf_unpack_bytes(&buf, &WORLD.map.water_depth[start_idx], MAP_CHUNK_WIDTH,
                      "water_depth_row");
  }

  // Unpack entity count
  uint32_t entity_count = bbuf_unpack_u32(&buf, "entity_count");

  // Build old_index -> new_index remapping table
  // Use 0 as sentinel for "not mapped" (entity 0 is reserved/invalid)
  EntityIndex remap[MAX_ENTITIES];
  memset(remap, 0, sizeof(remap));

  // Unpack each entity and build remap table
  for (uint32_t i = 0; i < entity_count; i++) {
    EntityIndex old_index = bbuf_unpack_u16(&buf, "entity_old_index");
    EntityIndex new_index = entity_unpack(&buf);
    remap[old_index] = new_index;
  }

  // Fixup parent references using remap table
  for (uint32_t i = 0; i < MAX_ENTITIES; i++) {
    EntityIndex entity = remap[i];
    if (entity == 0)
      continue;

    if (HAS_PART(Parent, entity)) {
      EntityIndex old_parent = PART(Parent, entity);
      EntityIndex new_parent = remap[old_parent];

      if (new_parent != 0) {
        // Parent was in this chunk, update reference
        PART(Parent, entity) = new_parent;
      } else {
        // Parent was not in this chunk, remove reference
        REMOVE_PART(Parent, entity);
      }
    }
  }
}

static void page_in_chunk(int chunk_x, int chunk_y) {
  assert(chunk_x >= 0 && chunk_x < MAP_CHUNK_WINDOW_X);
  assert(chunk_y >= 0 && chunk_y < MAP_CHUNK_WINDOW_Y);

  // Calculate world chunk coordinates
  int world_chunk_x = WORLD.worldmap.curr_chunk_x + (chunk_x - 1);
  int world_chunk_y = WORLD.worldmap.curr_chunk_y + (chunk_y - 1);
  assert(world_chunk_x >= 0 && world_chunk_x < MAP_CHUNK_TOTAL_X);
  assert(world_chunk_y >= 0 && world_chunk_y < MAP_CHUNK_TOTAL_X);

  int world_chunk_idx = world_chunk_y * MAP_CHUNK_TOTAL_X + world_chunk_x;
  ChunkState state = WORLD.worldmap.chunks[world_chunk_idx].state;

  // Skip if already loaded or loading
  if (state == CHUNK_LOADED || state == CHUNK_LOADING) {
    return;
  }

  // Mark as loading and request from storage
  WORLD.worldmap.chunks[world_chunk_idx].state = CHUNK_LOADING;

  uint64_t chunk_key = calc_chunk_key(world_chunk_x, world_chunk_y);
  output_message("Loading chunk (%d, %d)", world_chunk_x, world_chunk_y);
  host_load_chunk(chunk_key);

  // The callback game_chunk_loaded() will either deserialize or generate
}

static void page_out_chunk(int chunk_x, int chunk_y) {
  assert(chunk_x >= 0 && chunk_x < MAP_CHUNK_WINDOW_X);
  assert(chunk_y >= 0 && chunk_y < MAP_CHUNK_WINDOW_Y);

  // Calculate world chunk coordinates
  int world_chunk_x = WORLD.worldmap.curr_chunk_x + (chunk_x - 1);
  int world_chunk_y = WORLD.worldmap.curr_chunk_y + (chunk_y - 1);
  assert(world_chunk_x >= 0 && world_chunk_x < MAP_CHUNK_TOTAL_X);
  assert(world_chunk_y >= 0 && world_chunk_y < MAP_CHUNK_TOTAL_X);

  int world_chunk_idx = world_chunk_y * MAP_CHUNK_TOTAL_X + world_chunk_x;
  if (WORLD.worldmap.chunks[world_chunk_idx].state != CHUNK_LOADED) {
    return; // Not loaded, nothing to save
  }

  // Serialize chunk to stack-allocated buffer (512KB should be enough)
  uint8_t buffer[512 * 1024];
  ByteBuffer buf = {
      .size = 0,
      .capacity = sizeof(buffer),
      .data = buffer,
  };

  EntitySet entities_to_free;
  serialize_chunk(chunk_x, chunk_y, &buf, &entities_to_free);

  // Compress the serialized data
  int uncompressed_size = buf.size;
  int max_compressed_size = sdefl_bound(uncompressed_size);

  // Use static buffer for compression output (512KB should be enough)
  static uint8_t compressed_buffer[512 * 1024];
  assert(max_compressed_size <= (int)sizeof(compressed_buffer));

  struct sdefl sdefl_ctx = {0};
  int compressed_size = sdeflate(&sdefl_ctx, compressed_buffer, buf.data,
                                 uncompressed_size, SDEFL_LVL_DEF);

  // Store compressed chunk to host storage
  uint64_t chunk_key = calc_chunk_key(world_chunk_x, world_chunk_y);
  output_message("Saving chunk (%d, %d): %d -> %d bytes (%.1f%%)",
                 world_chunk_x, world_chunk_y, uncompressed_size,
                 compressed_size, 100.0f * compressed_size / uncompressed_size);
  host_store_chunk(chunk_key, compressed_buffer, compressed_size);

  // Free all entities that were saved (including descendants)
  entityset_free(&entities_to_free);

  // Mark as unloaded
  WORLD.worldmap.chunks[world_chunk_idx].state = CHUNK_UNLOADED;
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
          // Page out chunk (saves and frees entities, marks as CHUNK_UNLOADED)
          page_out_chunk(cx, cy);
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
      // Entity has been shifted out of the active window (should not happen!
      // page out logic should have stowed these entities)
      if (entity_is_player(i)) {
        // Don't despawn the player! Just clamp their position
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
      page_in_chunk(chunk_x, chunk_y);
    }
  }
}