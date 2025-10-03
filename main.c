#include "world/world.h"
#include <stdio.h>

static EntityIndex spawn_player(int x, int y) {
  EntityIndex handle = entity_alloc();
  entity_add(handle, position, ((Position){x, y}));
  entity_add(handle, health, ((Health){100, 100}));
  return handle;
}

static void entity_set_parent(EntityIndex entity, EntityIndex parent) {
  entity_add(entity, parent, parent);
}

static void pick_up(EntityIndex actor, EntityIndex what) {
  entity_set_parent(what, actor);
  entity_mark(what, is_inventory);
  entity_remove(what, position);
}

static void equip(EntityIndex actor, EntityIndex what) {
  if (!entity_has(what, is_inventory)) {
    printf("Can only equip item from inventory.\n");
    return;
  }
  assert(entity_has(what, parent));
  if (actor != world.parent[what]) {
    printf("That is in someone else's inventory!\n");
    return;
  }
  entity_mark(what, is_equipped);
  entity_unmark(what, is_inventory);
}

static int query_inventory(EntityIndex entity, EntityIndex *inv, int inv_max) {
  int count = 0;
  world_query(i, INC(parent) INC(is_inventory)) {
    if (entity == world.parent[i]) {
      assert(count < inv_max);
      inv[count++] = i;
    }
  }
  return count;
}

int main() {
  EntityIndex player = spawn_player(0, 0);

  EntityIndex corpse = entity_alloc();
  entity_add(corpse, position, ((Position){5, 5}));
  entity_mark(corpse, is_dead);

  // Test: entities with position, excluding equipped items
  printf("Non-equipped positioned entities:\n");
  world_query(i, INC(position) EXC(is_dead)) {
    printf("  entity %u at (%u, %u)\n", i, world.position[i].x,
           world.position[i].y);
  }

  // Test: multiple includes
  world_query(i, INC(position) INC(health)) {
    printf("  Living at (%u, %u) with %u HP\n", world.position[i].x,
           world.position[i].y, world.health[i].curr_health);
  }

  return 0;
}
