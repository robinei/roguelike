#include "../world.h"

void on_entity_unpacked(EntityIndex entity) {
  // Part:TurnSchedule Event:on_entity_unpacked
  if (HAS_PART(TurnSchedule, entity)) {
    int16_t delay = PART(TurnSchedule, entity).delay;
    DISABLE_PART(TurnSchedule, entity);
    turn_queue_insert(entity, delay);
  }
}
