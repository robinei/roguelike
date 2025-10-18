#include "map.h"
#include "random.h"

bool map_get_random_passable(Map *map, int region_x, int region_y,
                             int region_width, int region_height,
                             Position *out_pos, int max_attempts) {
  for (int attempt = 0; attempt < max_attempts; attempt++) {
    int x = region_x + random64() % region_width;
    int y = region_y + random64() % region_height;

    if (map->cells[y * MAP_WIDTH_MAX + x].passable) {
      out_pos->x = x;
      out_pos->y = y;
      return true;
    }
  }

  return false;
}
