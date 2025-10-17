#include "../world.h"

void entity_event_take_action_murder(EntityIndex entity, Goal *goal) {
  if (entity_is_alive(goal->target_entity)) {
    goal->is_finished = true;
    return;
  }
}