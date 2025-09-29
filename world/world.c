#include "world.h"

WorldState world;

EntityHandle allocate_entity(void) {
  uint16_t index;
  if (world.freelist_count > 0) {
    index = world.freelist[--world.freelist_count];
  } else {
    assert(world.entity_count < MAX_ENTITIES);
    index = world.entity_count++;
  }
  return (EntityHandle){++world.generation[index], index};
}

#define DO_CLEAR_COMPONENT_BIT(type, name) CLEAR_COMPONENT_BIT(index, name);

void free_entity(EntityHandle handle) {
  uint32_t index = unwrap_handle(handle);

  FOREACH_COMPONENT(DO_CLEAR_COMPONENT_BIT)

  assert(world.freelist_count < MAX_ENTITIES);
  world.freelist[world.freelist_count++] = index;
}
