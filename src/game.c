#include "game.h"
#include "actions/actions.h"
#include "common.h"
#include "components.h"
#include "particles.h"
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

void game_init(WorldState *world) {
  active_world = world;

  // entity at index 0 should not be used (index 0 should mean "no entity")
  entity_alloc();

  EntityIndex turn_index = entity_alloc();
  world->turn_entity = entity_handle_from_index(turn_index);
  turn_queue_insert(turn_index, TURN_INTERVAL);

  WORLD.player = entity_handle_from_index(spawn_player(0, 0));
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

void game_tick(WorldState *world, uint64_t tick) {
  active_world = world;
  particle_emit_system_tick();
}

void game_frame(WorldState *world, double dt) {
  active_world = world;
  particles_update(dt);
}

static void process_turn_entity(void) {
  // reduce delay for all entities by TURN_INTERVAL once each turn, to avoid
  // delay just growing endlessly. it would work even if it did grow endlessly,
  // since we just always schedule the entity with the lowest delay (remember
  // actions add their cost to the entity's delay)
  world_query(i, BITS(turn_schedule)) {
    WORLD.turn_schedule[i].delay -= TURN_INTERVAL;
  }

  // TODO: per-turn logic (regen, DOTs, cooldowns, etc.)
}

static int16_t execute_player_action(InputCommand command) {
  EntityIndex player = entity_handle_to_index(WORLD.player);

  // TODO: implement actual actions
  // For now, just basic movement/wait
  switch (command) {
  case INPUT_CMD_PERIOD:
    // Wait - costs normal turn
    return TURN_INTERVAL;

  case INPUT_CMD_UP:
  case INPUT_CMD_UP_RIGHT:
  case INPUT_CMD_RIGHT:
  case INPUT_CMD_DOWN_RIGHT:
  case INPUT_CMD_DOWN:
  case INPUT_CMD_DOWN_LEFT:
  case INPUT_CMD_LEFT:
  case INPUT_CMD_UP_LEFT:
    action_move(player, (Direction)(command - INPUT_CMD_UP));
    return TURN_INTERVAL;

  default:
    return TURN_INTERVAL;
  }
}

static void process_npc_turn(EntityIndex entity) {
  // TODO: AI logic for NPCs
  // For now, just wait
  turn_queue_insert(entity, TURN_INTERVAL);
}

static void run_queue_until_player_turn(void) {
  while (WORLD.turn_queue.count > 0) {
    EntityHandle next = turn_queue_peek();

    // If it's the player's turn, stop
    if (entity_handle_equals(next, WORLD.player)) {
      break;
    }

    // Pop and process the entity
    EntityHandle h = turn_queue_pop();
    EntityIndex entity = entity_handle_to_index(h);

    if (entity_handle_equals(h, WORLD.turn_entity)) {
      process_turn_entity();
      turn_queue_insert(entity, TURN_INTERVAL);
    } else {
      // It's an NPC
      process_npc_turn(entity);
    }
  }
}

void game_input(WorldState *world, InputCommand command) {
  active_world = world;

  // 1. Run queue until it's the player's turn (safety check)
  run_queue_until_player_turn();

  // At this point, player should be at the front of the queue
  EntityHandle next = turn_queue_peek();
  assert(entity_handle_equals(next, WORLD.player));

  // 2. Pop player, execute their action, and re-insert with action cost
  turn_queue_pop();
  int16_t action_cost = execute_player_action(command);
  EntityIndex player = entity_handle_to_index(WORLD.player);
  turn_queue_insert(player, action_cost);

  // 3. Run queue until it's the player's turn again
  run_queue_until_player_turn();
}

void game_render(WorldState *world, PlatformContext *platform) {
  active_world = world;

  static CommandBuffer cmd_buf;
  cmdbuf_clear(&cmd_buf);

  // Get player position for camera centering
  EntityIndex player_idx = entity_handle_to_index(world->player);
  int camera_center_x = 0;
  int camera_center_y = 0;

  if (entity_has(player_idx, position)) {
    camera_center_x = world->position[player_idx].x;
    camera_center_y = world->position[player_idx].y;
  }

  // Calculate viewport dimensions in tiles
  int viewport_tiles_x = platform->viewport_width_px / platform->tile_size;
  int viewport_tiles_y = platform->viewport_height_px / platform->tile_size;

  // Calculate top-left corner of viewport in world coordinates
  int viewport_left = camera_center_x - viewport_tiles_x / 2;
  int viewport_top = camera_center_y - viewport_tiles_y / 2;

  // Draw visible tiles
  for (int screen_y = 0; screen_y < viewport_tiles_y; screen_y++) {
    for (int screen_x = 0; screen_x < viewport_tiles_x; screen_x++) {
      int world_x = viewport_left + screen_x;
      int world_y = viewport_top + screen_y;

      // Check if tile is within map bounds
      if (world_x < 0 || world_x >= (int)world->map.width || world_y < 0 ||
          world_y >= (int)world->map.height) {
        // Out of bounds - skip (background already cleared)
        continue;
      }

      int tile = TILE_FLOOR;

      // Draw checkerboard pattern as test
      if ((world_x + world_y) % 2 == 0) {
        tile = 0; // First tile
      } else {
        tile = 1; // Second tile
      }

      int px = screen_x * platform->tile_size;
      int py = screen_y * platform->tile_size;
      cmdbuf_tile(&cmd_buf, platform, ATLAS_TILES, tile, px, py,
                  platform->tile_size, platform->tile_size);
    }
  }

  // Draw player at center of screen
  if (entity_has(player_idx, position)) {
    int player_screen_x = (viewport_tiles_x / 2) * platform->tile_size;
    int player_screen_y = (viewport_tiles_y / 2) * platform->tile_size;
    cmdbuf_tile(&cmd_buf, platform, ATLAS_TILES, TILE_PLAYER, player_screen_x,
                player_screen_y, platform->tile_size, platform->tile_size);
  }

  // Draw message log at bottom of screen
  #define MESSAGE_DISPLAY_LINES 5
  int message_viewport_tiles = MESSAGE_DISPLAY_LINES;

  // Check if the bottom-most tile is partial (would cut off last message line)
  bool bottom_tile_partial =
      (platform->viewport_height_px % platform->tile_size) != 0;

  // If bottom tile is partial, start messages one tile higher
  int viewport_start_y = viewport_tiles_y - message_viewport_tiles;
  if (bottom_tile_partial && viewport_start_y > 0) {
    viewport_start_y--;
  }
  if (viewport_start_y < 0)
    viewport_start_y = 0;

  int message_area_y = viewport_start_y * platform->tile_size;

  // Draw messages from circular buffer
  int messages_to_show = MESSAGE_DISPLAY_LINES;
  if (messages_to_show > (int)world->messages_count) {
    messages_to_show = world->messages_count;
  }

  // TODO: Support message scrolling - need to pass scroll offset to game_render
  int message_scroll_offset = 0;
  int start_msg_idx =
      (int)world->messages_count - messages_to_show - message_scroll_offset;
  if (start_msg_idx < 0)
    start_msg_idx = 0;

  for (int i = 0;
       i < messages_to_show && start_msg_idx + i < (int)world->messages_count;
       i++) {
    int msg_idx =
        (world->messages_first + start_msg_idx + i) % MESSAGE_COUNT_MAX;
    const char *text = world->messages[msg_idx].text;

    // Draw each character with semi-transparent background
    int x = 0;
    int y = message_area_y + i * platform->tile_size;
    for (const char *p = text; *p; p++) {
      // Draw background rect for this glyph
      cmdbuf_rect(&cmd_buf, platform, x, y, platform->tile_size,
                  platform->tile_size, RGBA(0, 0, 0, 192));

      // Draw the character glyph (CP437 layout: 16x16 grid)
      unsigned char ch = (unsigned char)*p;
      cmdbuf_tile(&cmd_buf, platform, ATLAS_FONT, ch, x, y,
                  platform->tile_size, platform->tile_size);

      x += platform->tile_size;
    }
  }

  // Flush any remaining commands
  if (cmd_buf.count > 0) {
    platform->execute_render_commands(platform->impl_data, &cmd_buf);
  }
}