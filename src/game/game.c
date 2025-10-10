#include "game.h"
#include "actions/actions.h"
#include "common.h"
#include "components.h"
#include "particles.h"
#include "random.h"
#include "render_api.h"
#include "turn_queue.h"
#include "world.h"

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

static EntityIndex spawn_player(int x, int y) {
  EntityIndex player = entity_alloc();
  entity_add(player, position, ((Position){x, y}));
  entity_add(player, health, HEALTH_FULL);
  turn_queue_insert(player, 0);
  return player;
}

static EntityIndex spawn_monster(int x, int y) {
  EntityIndex player = entity_alloc();
  entity_add(player, position, ((Position){x, y}));
  entity_add(player, health, HEALTH_FULL);
  turn_queue_insert(player, 0);
  return player;
}

void game_init(WorldState *world) {
  active_world = world;

  // entity at index 0 should not be used (index 0 should mean "no entity")
  entity_alloc();

  EntityIndex turn_index = entity_alloc();
  world->turn_entity = entity_handle_from_index(turn_index);
  turn_queue_insert(turn_index, TURN_INTERVAL);

  WORLD.player = entity_handle_from_index(spawn_player(0, 0));

  spawn_monster(10, 10);
  spawn_monster(0, 1);

  WORLD.map.width = 64;
  WORLD.map.height = 64;

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

  // tick handling
  WORLD.tick_accumulator += dt;
  const double TICK_INTERVAL = 0.1; // 100ms = 10Hz
  while (WORLD.tick_accumulator >= TICK_INTERVAL) {
    game_tick(world, WORLD.tick_counter++);
    WORLD.tick_accumulator -= TICK_INTERVAL;
  }

  particles_update(dt);

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

void game_render(WorldState *world, PlatformContext *platform) {
  active_world = world;

  CommandBuffer cmd_buf = {0};

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
  float camera_center_px = camera_center_x * platform->tile_size;
  float camera_center_py = camera_center_y * platform->tile_size;

  // Calculate top-left corner of viewport in pixels
  float viewport_left_px =
      camera_center_px - platform->viewport_width_px / 2.0f;
  float viewport_top_px =
      camera_center_py - platform->viewport_height_px / 2.0f;

  // Calculate top-left tile and pixel offset
  int start_tile_x = (int)(viewport_left_px / platform->tile_size);
  int start_tile_y = (int)(viewport_top_px / platform->tile_size);
  int offset_x = (int)(viewport_left_px - start_tile_x * platform->tile_size);
  int offset_y = (int)(viewport_top_px - start_tile_y * platform->tile_size);

  // Draw visible tiles
  int screen_y = -offset_y;
  for (int tile_y = start_tile_y; screen_y < platform->viewport_height_px;
       tile_y++) {
    int screen_x = -offset_x;
    for (int tile_x = start_tile_x; screen_x < platform->viewport_width_px;
         tile_x++) {
      // Check if tile is within map bounds
      if (tile_x >= 0 && tile_x < (int)world->map.width && tile_y >= 0 &&
          tile_y < (int)world->map.height) {

        int tile = TILE_FLOOR;

        // Draw checkerboard pattern as test
        if ((tile_x + tile_y) % 2 == 0) {
          tile = 0; // First tile
        } else {
          tile = 1; // Second tile
        }

        cmdbuf_tile(&cmd_buf, platform, tile, screen_x, screen_y,
                    platform->tile_size, platform->tile_size);
      }
      screen_x += platform->tile_size;
    }
    screen_y += platform->tile_size;
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
    float world_px = world_x * platform->tile_size;
    float world_py = world_y * platform->tile_size;
    int screen_x = (int)(world_px - viewport_left_px);
    int screen_y = (int)(world_py - viewport_top_px);

    // For now, all entities are rendered as TILE_PLAYER
    // TODO: Use glyph component or similar to determine tile
    cmdbuf_tile(&cmd_buf, platform, TILE_PLAYER, screen_x, screen_y,
                platform->tile_size, platform->tile_size);
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
    int y = platform->viewport_height_px -
            (messages_to_show - i) * platform->tile_size;

    // Draw each character with semi-transparent background
    int x = 0;
    for (const char *p = text; *p; p++) {
      // Draw background rect for this glyph
      cmdbuf_rect(&cmd_buf, platform, x, y, platform->tile_size,
                  platform->tile_size, RGBA(0, 0, 0, 192));

      // Draw the character glyph (glyphs start at FONT_BASE_INDEX in combined atlas)
      unsigned char ch = (unsigned char)*p;
      cmdbuf_tile(&cmd_buf, platform, FONT_BASE_INDEX + ch, x, y,
                  platform->tile_size, platform->tile_size);

      x += platform->tile_size;
    }
  }

  // Flush any remaining commands
  cmdbuf_flush(&cmd_buf, platform);
}

#ifdef __wasm__
// Imported from JavaScript
extern void execute_render_commands(void *impl_data,
                                    const CommandBuffer *buffer);

void game_render_wasm(WorldState *world, int viewport_width_px,
                      int viewport_height_px, int tile_size) {
  PlatformContext platform = {
      .viewport_width_px = viewport_width_px,
      .viewport_height_px = viewport_height_px,
      .tile_size = tile_size,
      .execute_render_commands = execute_render_commands,
      .impl_data = NULL, // Not used in WASM
  };

  game_render(world, &platform);
}
#endif
