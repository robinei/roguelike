#include "world/world.h"
#include <stdio.h>

static Entity spawn_player(int x, int y) {
  Entity handle = entity_alloc();
  entity_add_component(handle, position, ((Position){x, y}));
  entity_add_component(handle, health, ((Health){100, 100}));
  return handle;
}

static void entity_set_parent(Entity entity, Entity parent) {
  entity_add_component(entity, parent, parent);
}

static void pick_up(Entity actor, Entity what) {
  entity_set_parent(what, actor);
  entity_add_marker(what, is_inventory);
  entity_remove_component(what, position);
}

static void equip(Entity actor, Entity what) {
  if (!entity_has_component(what, is_inventory)) {
    printf("Can only equip item from inventory.\n");
    return;
  }
  assert(entity_has_component(what, parent));
  if (!entity_eq(actor, world.parent[entity_to_index(what)])) {
    printf("That is in someone else's inventory!\n");
    return;
  }
  entity_add_marker(what, is_equipped);
  entity_remove_marker(what, is_inventory);
}

static int query_inventory(Entity entity, Entity *inv, int inv_max) {
  int count = 0;
  world_query(i, INC(parent) INC(is_inventory)) {
    if (entity_eq(entity, world.parent[i])) {
      assert(count < inv_max);
      inv[count++] = entity_from_index(i);
    }
  }
  return count;
}

int main() {
  Entity player = spawn_player(0, 0);

  Entity corpse = entity_alloc();
  entity_add_component(corpse, position, ((Position){5, 5}));
  entity_add_marker(corpse, is_dead);

  // Test: entities with position, excluding equipped items
  printf("Non-equipped positioned entities:\n");
  world_query(i, INC(position) EXC(is_dead)) {
    printf("  Entity %u at (%u, %u)\n", i, world.position[i].x,
           world.position[i].y);
  }

  // Test: multiple includes
  world_query(i, INC(position) INC(health)) {
    printf("  Living at (%u, %u) with %u HP\n", world.position[i].x,
           world.position[i].y, world.health[i].curr_health);
  }

  return 0;
}
