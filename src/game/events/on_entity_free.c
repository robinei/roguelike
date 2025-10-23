#include "../world.h"

void on_entity_free(EntityIndex entity) {
  // Part:TurnSchedule Event:on_entity_free
  if (HAS_PART(TurnSchedule, entity)) {
    turn_queue_remove(entity);
  }
}
