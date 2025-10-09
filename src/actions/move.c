#include "../world.h"

void action_move(EntityIndex entity, Direction dir) {
  if (entity_has(entity, position)) {
    Position *pos = &WORLD.position[entity];

    int x = pos->x + dir_dx(dir);
    int y = pos->y + dir_dy(dir);
    if (x < 0 || y < 0 || x >= WORLD.map.width || y >= WORLD.map.height) {
      return;
    }

    pos->x = x;
    pos->y = y;
  }
}