#include "../fov.h"
#include "../world.h"
#include "actions.h"

void on_player_moved(void) {
  EntityIndex player_idx = entity_handle_to_index(WORLD.player);
  if (entity_has(player_idx, position)) {
    Position *pos = &WORLD.position[player_idx];
    fov_compute(&WORLD.map, pos->x, pos->y, PLAYER_FOV_RADIUS);
  }
}

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

  if (!WORLD.map.cells[y * MAP_WIDTH_MAX + x].passable) {
    return;
  }

  world_query(i, BITS(position)) {
    Position *pos2 = &WORLD.position[i];
    if (x == pos2->x && y == pos2->y) {
      action_combat(entity, i);
      return;
    }
  }

  turn_queue_add_delay(entity, TURN_INTERVAL);

  WORLD.anim = (ActionAnim){.type = ACTION_ANIM_MOVE,
                            .actor = entity_handle_from_index(entity),
                            .move = {.from = *pos, .to = {x, y}}};

  *pos = (Position){x, y};

  on_player_moved();
}
