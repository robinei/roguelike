#include "world/world.h"

int main() {
  ITER_QUERY(i, (position, health)) {
    uint16_t x = world.position[i].x;
    uint16_t hp = world.health[i].curr_health;
  }

  return 0;
}
