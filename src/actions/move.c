#include "../world.h"

void action_move(EntityIndex entity, Direction dir) {
  if (!entity_has(entity, position)) {
    return;
  }

  Position *pos = &WORLD.position[entity];
  int x = pos->x + dir_dx(dir);
  int y = pos->y + dir_dy(dir);
  if (x < 0 || y < 0 || x >= WORLD.map.width || y >= WORLD.map.height) {
    return;
  }

  WORLD.anim = (ActionAnim){.type = ACTION_ANIM_MOVE,
                            .actor = entity_handle_from_index(entity),
                            .move = {.from = *pos, .to = {x, y}}};

  *pos = (Position){x, y};
}
