#include "map.h"
#include "random.h"

bool map_get_random_passable(Map *map, Position *out_pos, int max_attempts) {
  for (int attempt = 0; attempt < max_attempts; attempt++) {
    int x = random64() % map->width;
    int y = random64() % map->height;

    if (map->cells[y * MAP_WIDTH_MAX + x].passable) {
      out_pos->x = x;
      out_pos->y = y;
      return true;
    }
  }

  return false;
}
