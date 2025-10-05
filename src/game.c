#include "game.h"
#include "components.h"
#include "particles.h"
#include "world.h"

static EntityIndex get_position_entity(EntityIndex entity) {
  for (;;) {
    if (entity_has(entity, position)) {
      return entity;
    }
    if (!entity_has(entity, parent)) {
      return 0;
    }
    entity = world.parent[entity];
  }
}

static void particle_emit_system_tick() {
  world_query(i, BITS(particle_emitter)) {
    EntityIndex pos_index = get_position_entity(i);
    if (!pos_index) {
      // can't spawn without position
      continue;
    }

    Position *pos = world.position + pos_index;
    ParticleEmitter *pe = world.particle_emitter + i;

    if (pe->countdown_ticks > 0 && --pe->countdown_ticks == 0) {
      float x = (float)pos->x + 0.5f;
      float y = (float)pos->y + 0.5f;
      particles_spawn(pe->particle_type, x, y);
      pe->countdown_ticks = particles_gen_spawn_interval(pe->particle_type);
    }
  }
}

void game_tick(uint64_t tick) { particle_emit_system_tick(); }

void game_frame(double dt) {}
