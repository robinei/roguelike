#include "game.h"
#include "actions/actions.h"
#include "common.h"
#include "components.h"
#include "fov.h"
#include "map.h"
#include "mapgen/mapgen.h"
#include "particles.h"
#include "prnf.h"
#include "random.h"
#include "render_api.h"
#include "turn_queue.h"
#include "world.h"

static void update_player_fov(void) {
  EntityIndex player_idx = entity_handle_to_index(WORLD.player);
  if (entity_has(player_idx, position)) {
    Position *pos = &WORLD.position[player_idx];
    fov_compute(&WORLD.map, pos->x, pos->y, PLAYER_FOV_RADIUS);
  }
}

static void particle_emit_system_tick() {
  world_query(i, BITS(particle_emitter)) {
    EntityIndex pos_index = get_position_ancestor(i);
    if (!pos_index) {
      // can't spawn without position
      continue;
    }

    Position *pos = WORLD.position + pos_index;
    ParticleEmitter *pe = WORLD.particle_emitter + i;

    if (pe->countdown_ticks > 0 && --pe->countdown_ticks == 0) {
      float x = (float)pos->x + 0.5f;
      float y = (float)pos->y + 0.5f;
      particles_spawn(pe->particle_type, x, y);
      pe->countdown_ticks = particles_gen_spawn_interval(pe->particle_type);
    }
  }
}

static EntityIndex spawn_player(void) {
  Position pos;
  if (!map_get_random_passable(&WORLD.map, &pos, 100)) {
    // Fallback to 0,0 if no passable position found
    pos.x = 0;
    pos.y = 0;
  }

  EntityIndex player = entity_alloc();
  entity_add(player, position, pos);
  entity_add(player, health, HEALTH_FULL);
  turn_queue_insert(player, 0);
  return player;
}

static EntityIndex spawn_monster(void) {
  Position pos;
  if (!map_get_random_passable(&WORLD.map, &pos, 100)) {
    // Failed to find passable position, don't spawn
    return 0;
  }

  EntityIndex monster = entity_alloc();
  entity_add(monster, position, pos);
  entity_add(monster, health, HEALTH_FULL);
  turn_queue_insert(monster, 0);
  return monster;
}

void game_init(WorldState *world, uint64_t rng_seed) {
  active_world = world;
  world->rng_state = rng_seed;

  // entity at index 0 should not be used (index 0 should mean "no entity")
  entity_alloc();

  EntityIndex turn_index = entity_alloc();
  world->turn_entity = entity_handle_from_index(turn_index);
  turn_queue_insert(turn_index, TURN_INTERVAL);

  // Generate map before spawning entities
  WORLD.map.width = 128;
  WORLD.map.height = 128;

  BSPGenParams bsp_params = {
      .max_depth = 5,        // More depth = more rooms
      .min_region_size = 10, // Stop splitting small regions
      .min_child_size = 6,   // Minimum size for each child after split
      .split_threshold = 14, // Split along longer axis if above this
      .min_room_size = 4,    // Minimum room dimensions
      .room_padding = 2,     // Padding around rooms within regions
      .map_border = 1,       // Keep 1 tile away from map edge
  };
  mapgen_bsp(&WORLD.map, &bsp_params);

  // Spawn player and monsters in random passable positions
  WORLD.player = entity_handle_from_index(spawn_player());

  spawn_monster();
  spawn_monster();
  spawn_monster();

  // Compute initial FOV for player
  update_player_fov();

  // Generate test messages
  output_message("Welcome to the dungeon!");
  output_message("You hear strange noises in the distance.");
  output_message("A cold wind blows through the corridor.");
  output_message("You find a rusty sword lying on the ground.");
  output_message("The walls are covered in ancient runes.");
  output_message("You step on something crunchy.");
  output_message("A rat scurries past your feet.");
  output_message("The air smells of decay and mold.");
  output_message("You hear dripping water somewhere nearby.");
  output_message("Your torch flickers ominously.");
  output_message("You feel like you're being watched.");
  output_message("The door ahead is locked.");
  output_message("You found a key!");
  output_message("The key fits the lock perfectly.");
  output_message("The door creaks open slowly.");
  output_message("You enter a large chamber.");
  output_message("Something growls in the darkness.");
  output_message("Roll for initiative!");
}

static void game_tick(WorldState *world, uint64_t tick) {
  active_world = world;
  (void)tick; // Unused for now
  particle_emit_system_tick();
}

static void process_turn_entity(void) {
  EntityIndex entity = entity_handle_to_index(WORLD.turn_entity);
  turn_queue_add_delay(entity, TURN_INTERVAL);

  // reduce delay for all entities by TURN_INTERVAL once each turn, to avoid
  // delay just growing endlessly. it would work even if it did grow endlessly,
  // since we just always schedule the entity with the lowest delay (remember
  // actions add their cost to the entity's delay)
  world_query(i, BITS(turn_schedule)) {
    WORLD.turn_schedule[i].delay -= TURN_INTERVAL;
  }

  // TODO: per-turn logic (regen, DOTs, cooldowns, etc.)
}

static void execute_player_action(InputCommand command) {
  EntityIndex player = entity_handle_to_index(WORLD.player);

  // TODO: implement actual actions
  // For now, just basic movement/wait
  switch (command) {
  case INPUT_CMD_PERIOD:
    // Wait - costs normal turn
    turn_queue_add_delay(player, TURN_INTERVAL);
    break;

  case INPUT_CMD_UP:
  case INPUT_CMD_UP_RIGHT:
  case INPUT_CMD_RIGHT:
  case INPUT_CMD_DOWN_RIGHT:
  case INPUT_CMD_DOWN:
  case INPUT_CMD_DOWN_LEFT:
  case INPUT_CMD_LEFT:
  case INPUT_CMD_UP_LEFT:
    action_move(player, (Direction)(command - INPUT_CMD_UP));
    break;

  default:
    break;
  }
}

static void process_npc_turn(EntityIndex entity) {
  // TODO: AI logic for NPCs
  // For now, just wait
  action_move(entity, random64() % 8);
}

void game_frame(WorldState *world, double dt) {
  active_world = world;

  // FPS calculation (update every second)
  WORLD.frame_time_accumulator += dt;
  WORLD.frame_count++;
  if (WORLD.frame_time_accumulator >= 1.0) {
    WORLD.fps = WORLD.frame_count / WORLD.frame_time_accumulator;
    WORLD.frame_time_accumulator = 0.0;
    WORLD.frame_count = 0;
  }

  // tick handling
  WORLD.tick_accumulator += dt;
  const double TICK_INTERVAL = 0.1; // 100ms = 10Hz
  while (WORLD.tick_accumulator >= TICK_INTERVAL) {
    game_tick(world, WORLD.tick_counter++);
    WORLD.tick_accumulator -= TICK_INTERVAL;
  }

  particles_update(dt);

  if (WORLD.anim.type != ACTION_ANIM_NONE) {
    EntityIndex actor = entity_handle_to_index(WORLD.anim.actor);
    if (entity_has(actor, position)) {
      int x = WORLD.position[actor].x;
      int y = WORLD.position[actor].y;
      if (!WORLD.map.cells[y * MAP_WIDTH_MAX + x].visible) {
        memset(&WORLD.anim, 0, sizeof(WORLD.anim));
      }
    }
  }

  // Advance action animation
  if (WORLD.anim.type != ACTION_ANIM_NONE) {
    const double ANIM_DURATION = 0.15; // 150ms per action
    WORLD.anim.progress += dt / ANIM_DURATION;

    if (WORLD.anim.progress >= 1.0) {
      // Animation complete, clear it
      WORLD.anim.type = ACTION_ANIM_NONE;
      WORLD.anim.progress = 0.0;
    }
  }

  // If no animation playing, process the turn queue
  if (WORLD.anim.type == ACTION_ANIM_NONE && WORLD.turn_queue.count > 0) {
    EntityHandle next = turn_queue_peek();

    if (entity_handle_equals(next, WORLD.player)) {
      // Player's turn - do we have input?
      if (WORLD.next_player_input != INPUT_CMD_NONE) {
        execute_player_action(WORLD.next_player_input);
        WORLD.next_player_input = INPUT_CMD_NONE;
      }
      // No input? Just wait (don't pop from queue)
    } else if (entity_handle_equals(next, WORLD.turn_entity)) {
      process_turn_entity();
    } else {
      // NPC turn - will set anim if needed
      process_npc_turn(entity_handle_to_index(next));
    }
  }
}

void game_input(WorldState *world, InputCommand command) {
  active_world = world;

  // Just record the input - game_frame will process it
  WORLD.next_player_input = command;
}

// Helper to calculate darkness at a tile center for torch lighting
static uint8_t calc_tile_darkness(Map *map, int tile_x, int tile_y, float player_x, float player_y) {
  // Out of bounds
  if (tile_x < 0 || tile_x >= map->width || tile_y < 0 || tile_y >= map->height) {
    return 192; // Full darkness
  }

  if (!map->cells[tile_y * MAP_WIDTH_MAX + tile_x].visible) {
    return 192; // Non-visible: full darkness
  }

  // Visible - calculate torch lighting based on distance from tile center
  float tx = tile_x + 0.5f;
  float ty = tile_y + 0.5f;
  float dx = tx - player_x;
  float dy = ty - player_y;
  float dist_sq = dx * dx + dy * dy;

  if (dist_sq < 0.01f) {
    return 0; // At player position - no darkness
  }

  float dist = 1.0f / rsqrt(dist_sq);

  if (dist < PLAYER_TORCH_RADIUS) {
    float fade = dist / PLAYER_TORCH_RADIUS;
    return (uint8_t)(fade * 192.0f);
  }

  return 192; // Beyond torch radius - full darkness
}

// Helper to calculate darkness at a corner by averaging surrounding tile centers
static uint8_t calc_corner_darkness(Map *map, int tile_x, int tile_y, int corner_x, int corner_y, float player_x, float player_y) {
  // Sample the 4 neighboring tiles around this corner
  // corner_x and corner_y are 0 or 1, indicating which corner of the tile
  int nx0 = tile_x + corner_x - 1;
  int ny0 = tile_y + corner_y - 1;
  int nx1 = tile_x + corner_x;
  int ny1 = tile_y + corner_y - 1;
  int nx2 = tile_x + corner_x - 1;
  int ny2 = tile_y + corner_y;
  int nx3 = tile_x + corner_x;
  int ny3 = tile_y + corner_y;

  int sum = calc_tile_darkness(map, nx0, ny0, player_x, player_y) +
            calc_tile_darkness(map, nx1, ny1, player_x, player_y) +
            calc_tile_darkness(map, nx2, ny2, player_x, player_y) +
            calc_tile_darkness(map, nx3, ny3, player_x, player_y);

  return (uint8_t)(sum / 4);
}

void game_render(WorldState *world, RenderContext *ctx) {
  active_world = world;

  // Use static to avoid stack overflow (GeometryBuilder is ~128KB)
  static GeometryBuilder geom;
  geobuilder_init(&geom, ctx);

  // Get player position for camera centering
  EntityIndex player_idx = entity_handle_to_index(world->player);
  float camera_center_x = 0.0f;
  float camera_center_y = 0.0f;

  if (entity_has(player_idx, position)) {
    Position *pos = &world->position[player_idx];
    camera_center_x = (float)pos->x;
    camera_center_y = (float)pos->y;

    // If player is animating, use interpolated position
    if (WORLD.anim.type == ACTION_ANIM_MOVE &&
        entity_handle_to_index(WORLD.anim.actor) == player_idx) {
      float t = WORLD.anim.progress;
      camera_center_x = WORLD.anim.move.from.x +
                        (WORLD.anim.move.to.x - WORLD.anim.move.from.x) * t;
      camera_center_y = WORLD.anim.move.from.y +
                        (WORLD.anim.move.to.y - WORLD.anim.move.from.y) * t;
    }
  }

  // Calculate camera position in pixels (center on player's interpolated
  // position)
  float camera_center_px = camera_center_x * ctx->tile_size;
  float camera_center_py = camera_center_y * ctx->tile_size;

  // Calculate top-left corner of viewport in pixels
  float viewport_left_px = camera_center_px - ctx->viewport_width_px / 2.0f;
  float viewport_top_px = camera_center_py - ctx->viewport_height_px / 2.0f;

  // Calculate top-left tile and pixel offset
  int start_tile_x = (int)(viewport_left_px / ctx->tile_size);
  int start_tile_y = (int)(viewport_top_px / ctx->tile_size);
  int offset_x = (int)(viewport_left_px - start_tile_x * ctx->tile_size);
  int offset_y = (int)(viewport_top_px - start_tile_y * ctx->tile_size);

  // Get player position for torch lighting
  float player_tile_x = camera_center_x;
  float player_tile_y = camera_center_y;

  // Draw visible tiles with interpolated torch lighting
  int screen_y = -offset_y;
  for (int tile_y = start_tile_y; screen_y < ctx->viewport_height_px;
       tile_y++) {
    int screen_x = -offset_x;
    for (int tile_x = start_tile_x; screen_x < ctx->viewport_width_px;
         tile_x++) {
      // Check if tile is within map bounds
      if (tile_x >= 0 && tile_x < (int)world->map.width && tile_y >= 0 &&
          tile_y < (int)world->map.height) {

        int tile = world->map.cells[tile_y * MAP_WIDTH_MAX + tile_x].tile;
        geobuilder_tile(&geom, tile, screen_x, screen_y);

        // Check if this tile is visible
        bool tile_visible = world->map.cells[tile_y * MAP_WIDTH_MAX + tile_x].visible;

        if (tile_visible) {
          // Check if this tile has any lighting (to decide if we need expensive corner sampling)
          uint8_t tile_darkness = calc_tile_darkness(&world->map, tile_x, tile_y, player_tile_x, player_tile_y);

          if (tile_darkness < 192) {
            // Tile has some lighting - do full corner interpolation
            uint8_t tl_alpha = calc_corner_darkness(&world->map, tile_x, tile_y, 0, 0, player_tile_x, player_tile_y);
            uint8_t tr_alpha = calc_corner_darkness(&world->map, tile_x, tile_y, 1, 0, player_tile_x, player_tile_y);
            uint8_t bl_alpha = calc_corner_darkness(&world->map, tile_x, tile_y, 0, 1, player_tile_x, player_tile_y);
            uint8_t br_alpha = calc_corner_darkness(&world->map, tile_x, tile_y, 1, 1, player_tile_x, player_tile_y);

            // Draw darkness overlay with per-vertex colors
            Color tl = {0, 0, 0, tl_alpha};
            Color tr = {0, 0, 0, tr_alpha};
            Color bl = {0, 0, 0, bl_alpha};
            Color br = {0, 0, 0, br_alpha};
            geobuilder_rect_colored(&geom, screen_x, screen_y, ctx->tile_size,
                                    ctx->tile_size, tl, tr, bl, br);
          } else {
            // Tile is visible but outside torch radius - uniform darkness
            geobuilder_rect(&geom, screen_x, screen_y, ctx->tile_size,
                            ctx->tile_size, (Color){0, 0, 0, 192});
          }
        } else {
          // Non-visible tile - draw uniform full darkness
          geobuilder_rect(&geom, screen_x, screen_y, ctx->tile_size,
                          ctx->tile_size, (Color){0, 0, 0, 192});
        }
      }
      screen_x += ctx->tile_size;
    }
    screen_y += ctx->tile_size;
  }

  // Draw entities with position component
  world_query(i, BITS(position)) {
    Position *pos = &WORLD.position[i];

    // Start with entity's actual position (in tile coordinates)
    float world_x = (float)pos->x;
    float world_y = (float)pos->y;

    // If this entity is animating, interpolate between from and to positions
    if (WORLD.anim.type == ACTION_ANIM_MOVE &&
        entity_handle_to_index(WORLD.anim.actor) == i) {
      float t = WORLD.anim.progress;
      world_x = WORLD.anim.move.from.x +
                (WORLD.anim.move.to.x - WORLD.anim.move.from.x) * t;
      world_y = WORLD.anim.move.from.y +
                (WORLD.anim.move.to.y - WORLD.anim.move.from.y) * t;
    } else if (WORLD.anim.type == ACTION_ANIM_ATTACK &&
               entity_handle_to_index(WORLD.anim.actor) == i) {
      // Move slightly toward target and back (bump animation)
      EntityIndex target_idx = entity_handle_to_index(WORLD.anim.attack.target);
      if (entity_has(target_idx, position)) {
        Position *target_pos = &WORLD.position[target_idx];

        // Calculate direction to target
        float dx = target_pos->x - pos->x;
        float dy = target_pos->y - pos->y;

        // Normalize
        float len2 = dx * dx + dy * dy;
        if (len2 > 0.001f) {
          float s = rsqrt(len2);
          dx *= s;
          dy *= s;
        }

        // Bump distance: 0.3 tiles
        float t = WORLD.anim.progress;
        // Ease out and back: move forward in first half, back in second half
        float bump_amount = (t < 0.5f) ? t * 2.0f : (1.0f - t) * 2.0f;
        bump_amount *= 0.3f;

        world_x += dx * bump_amount;
        world_y += dy * bump_amount;
      }
    }

    // Convert world position to pixels, then to screen coordinates
    float world_px = world_x * ctx->tile_size;
    float world_py = world_y * ctx->tile_size;
    int screen_x = (int)(world_px - viewport_left_px);
    int screen_y = (int)(world_py - viewport_top_px);

    // For now, all entities are rendered as TILE_PLAYER
    // TODO: Use glyph component or similar to determine tile
    geobuilder_tile(&geom, TILE_PLAYER, screen_x, screen_y);
  }

// Draw message log at bottom of screen
#define MESSAGE_DISPLAY_LINES 5

  // Get the most recent N messages
  int messages_to_show = MESSAGE_DISPLAY_LINES;
  if (messages_to_show > (int)world->messages_count) {
    messages_to_show = world->messages_count;
  }

  for (int i = 0; i < messages_to_show; i++) {
    // Get the i-th most recent message (counting from end)
    int offset = (int)world->messages_count - messages_to_show + i;
    int msg_idx = (world->messages_first + offset) % MESSAGE_COUNT_MAX;
    const char *text = world->messages[msg_idx].text;

    // Position from bottom up
    int y = ctx->viewport_height_px - (messages_to_show - i) * ctx->tile_size;

    geobuilder_text(&geom, 0, y, TEXT_ALIGN_LEFT, (Color){.a = 192}, "%s",
                    text);
  }

  // Draw FPS in upper right corner
  if (WORLD.fps > 0.0f) {
    geobuilder_text(&geom, ctx->viewport_width_px, 0, TEXT_ALIGN_RIGHT,
                    (Color){.a = 192}, "%.1f FPS", (double)WORLD.fps);
  }

  // Flush any remaining vertices
  geobuilder_flush(&geom);
}

#ifdef __wasm__
extern unsigned char __heap_base;
extern unsigned char __data_end;
extern unsigned char __stack_pointer;

unsigned char *get_heap_base(void) { return &__heap_base; }

// WASM-friendly render function that takes viewport dimensions directly
// and uses the imported submit_geometry function
// Imported from JavaScript
extern void submit_geometry(void *impl_data, const Vertex *vertices,
                            int vertex_count);

void game_render_wasm(WorldState *world, int viewport_width_px,
                      int viewport_height_px, int tile_size, int atlas_width_px,
                      int atlas_height_px) {
  RenderContext ctx = {
      .viewport_width_px = viewport_width_px,
      .viewport_height_px = viewport_height_px,
      .tile_size = tile_size,
      .atlas_width_px = atlas_width_px,
      .atlas_height_px = atlas_height_px,
      .submit_geometry = submit_geometry,
      .impl_data = NULL, // Not used in WASM
  };

  game_render(world, &ctx);
}
#endif
