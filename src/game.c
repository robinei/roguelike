#include "game.h"
#include "components.h"
#include "particles.h"
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

void game_init(WorldState *world) {
  active_world = world;

  // entity at index 0 should not be used (index 0 should mean "no entity")
  entity_alloc();

  EntityIndex turn_index = entity_alloc();
  world->turn_entity = entity_handle_from_index(turn_index);
  turn_queue_insert(turn_index, TURN_INTERVAL);
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
    // Movement - costs normal turn
    // TODO: actually move the player
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