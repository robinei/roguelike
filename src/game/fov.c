#include "fov.h"
#include "common.h"

// Adapted from: http://www.roguebasin.com/index.php?title=Spiral_Path_FOV

#define MAX_RADIUS 300
#define TABLE_DIM (2 * MAX_RADIUS)
#define QUEUE_LENGTH (2 * TABLE_DIM)
#define MAX_ANGLE ((int)(1000000 * 2.0 * M_PI))

// Lookup tables
static int min_angle_table[TABLE_DIM * TABLE_DIM];
static int max_angle_table[TABLE_DIM * TABLE_DIM];
static int outer_angle_table[TABLE_DIM * TABLE_DIM];
static int outer_angle2_table[TABLE_DIM * TABLE_DIM];

// Reusable tables with state (zero-initialized, returned to zero after use)
static int min_lit_table[TABLE_DIM * TABLE_DIM];
static int max_lit_table[TABLE_DIM * TABLE_DIM];

// Reusable queue arrays
static int queue_x[QUEUE_LENGTH];
static int queue_y[QUEUE_LENGTH];

static bool tables_initialized = false;

static inline int calc_table_index(int x, int y) {
  return (y + MAX_RADIUS) * TABLE_DIM + (x + MAX_RADIUS);
}

static int to_angle(double a) {
  int result = (int)(1000000 * a);
  while (result < 0) {
    result += MAX_ANGLE;
  }
  while (result > MAX_ANGLE) {
    result -= MAX_ANGLE;
  }
  return result;
}

// Returns the angle from center of origin to a coordinate
static double coord_angle(int x, int y) { return atan2(y - 0.5, x - 0.5); }

// The minimum angle of the tile (smallest-angled corner)
static double calc_min_angle(int x, int y) {
  if (x == 0 && y == 0) {
    return 0.0; // origin special case
  }
  if (x >= 0 && y > 0) {
    return coord_angle(x + 1, y); // first quadrant
  }
  if (x < 0 && y >= 0) {
    return coord_angle(x + 1, y + 1); // second quadrant
  }
  if (x <= 0 && y < 0) {
    return coord_angle(x, y + 1); // third quadrant
  }
  return coord_angle(x, y); // fourth quadrant
}

// The maximum angle of the tile (largest-angled corner)
static double calc_max_angle(int x, int y) {
  if (x == 0 && y == 0) {
    return 2.0 * M_PI; // origin special case
  }
  if (x > 0 && y >= 0) {
    return coord_angle(x, y + 1); // first quadrant
  }
  if (x <= 0 && y > 0) {
    return coord_angle(x, y); // second quadrant
  }
  if (x < 0 && y <= 0) {
    return coord_angle(x + 1, y); // third quadrant
  }
  return coord_angle(x + 1, y + 1); // fourth quadrant
}

// The angle of the outer corner (on origin lines, the first outer corner)
static double calc_outer_angle(int x, int y) {
  if (x == 0 && y == 0) {
    return 0.0; // origin special case
  }
  if (x >= 0 && y > 0) {
    return coord_angle(x + 1, y + 1); // first quadrant with positive y axis
  }
  if (x < 0 && y >= 0) {
    return coord_angle(x, y + 1); // second quadrant with negative x axis
  }
  if (x <= 0 && y < 0) {
    return coord_angle(x, y); // third quadrant with negative y axis
  }
  return coord_angle(x + 1, y); // fourth quadrant with positive x axis
}

// Squares on the axes (x or y == 0) have a second outer corner
static double calc_outer_angle2(int x, int y) {
  if (x != 0 && y != 0) {
    return 0.0; // meaningless on non-axis squares
  }
  if (x > 0) {
    return coord_angle(x + 1, y + 1);
  }
  if (x < 0) {
    return coord_angle(x, y);
  }
  if (y > 0) {
    return coord_angle(x, y + 1);
  }
  if (y < 0) {
    return coord_angle(x + 1, y);
  }
  return 0.0; // meaningless on origin
}

static void init_tables(void) {
  if (tables_initialized) {
    return;
  }

  for (int y = -MAX_RADIUS; y < MAX_RADIUS; ++y) {
    for (int x = -MAX_RADIUS; x < MAX_RADIUS; ++x) {
      int table_index = (y + MAX_RADIUS) * TABLE_DIM + (x + MAX_RADIUS);
      min_angle_table[table_index] = to_angle(calc_min_angle(x, y));
      max_angle_table[table_index] = to_angle(calc_max_angle(x, y));
      outer_angle_table[table_index] = to_angle(calc_outer_angle(x, y));
      outer_angle2_table[table_index] = to_angle(calc_outer_angle2(x, y));
    }
  }

  tables_initialized = true;
}

// Forward declarations for local functions
static void mark(int x, int y, int min, int max, int *queue_head_ptr,
                 int *queue_tail_ptr);
static void test_mark(int x, int y, int min_lit_angle, int max_lit_angle,
                      int min_angle, int max_angle, int *queue_head_ptr,
                      int *queue_tail_ptr);

void fov_compute(Map *map, int origin_x, int origin_y, int radius) {
  init_tables();

  if (radius >= MAX_RADIUS) {
    radius = MAX_RADIUS - 1;
  }

  // Clear all visible flags
  for (int y = 0; y < map->height; ++y) {
    for (int x = 0; x < map->width; ++x) {
      map->cells[y * MAP_WIDTH_MAX + x].visible = 0;
    }
  }

  int arc_start = 0;
  int arc_end = MAX_ANGLE;

  int queue_head = 0;
  int queue_tail = 0;

  // Origin is always visible
  if (origin_x >= 0 && origin_x < map->width && origin_y >= 0 &&
      origin_y < map->height) {
    map->cells[origin_y * MAP_WIDTH_MAX + origin_x].visible = 1;
  }

  // Starting set: 4 squares at manhattan distance 1
  test_mark(1, 0, arc_start, arc_end, min_angle_table[calc_table_index(1, 0)],
            max_angle_table[calc_table_index(1, 0)], &queue_head, &queue_tail);
  test_mark(0, 1, arc_start, arc_end, min_angle_table[calc_table_index(0, 1)],
            max_angle_table[calc_table_index(0, 1)], &queue_head, &queue_tail);
  test_mark(-1, 0, arc_start, arc_end, min_angle_table[calc_table_index(-1, 0)],
            max_angle_table[calc_table_index(-1, 0)], &queue_head, &queue_tail);
  test_mark(0, -1, arc_start, arc_end, min_angle_table[calc_table_index(0, -1)],
            max_angle_table[calc_table_index(0, -1)], &queue_head, &queue_tail);

  while (queue_head != queue_tail) {
    // Dequeue one item
    int cur_x = queue_x[queue_head];
    int cur_y = queue_y[queue_head];
    queue_head = (queue_head + 1) % QUEUE_LENGTH;

    int table_index = calc_table_index(cur_x, cur_y);
    int min_angle = min_angle_table[table_index];
    int outer_angle = outer_angle_table[table_index];
    int outer_angle2 = outer_angle2_table[table_index];
    int max_angle = max_angle_table[table_index];
    int min_lit_angle = min_lit_table[table_index];
    int max_lit_angle = max_lit_table[table_index];

    // Clear lighting for this tile
    min_lit_table[table_index] = 0;
    max_lit_table[table_index] = 0;

    if (cur_x * cur_x + cur_y * cur_y < radius * radius) {
      // Check if within arc (we use full 360 degrees, so skip this check)
      // Mark as visible
      int world_x = origin_x + cur_x;
      int world_y = origin_y + cur_y;
      if (world_x >= 0 && world_x < map->width && world_y >= 0 &&
          world_y < map->height) {
        map->cells[world_y * MAP_WIDTH_MAX + world_x].visible = 1;
      }

      // Check if blocked (use passable flag inverted)
      bool blocked = false;
      if (world_x >= 0 && world_x < map->width && world_y >= 0 &&
          world_y < map->height) {
        blocked = !map->cells[world_y * MAP_WIDTH_MAX + world_x].passable;
      }

      if (!blocked) {
        // Not blocked - propagate light to children
        int child1_x = 0, child1_y = 0;
        if (cur_x == 0 && cur_y == 0) {
          child1_x = cur_x;
          child1_y = cur_y; // origin
        } else if (cur_x >= 0 && cur_y > 0) {
          child1_x = cur_x + 1;
          child1_y = cur_y; // quadrant 1
        } else if (cur_x < 0 && cur_y >= 0) {
          child1_x = cur_x;
          child1_y = cur_y + 1; // quadrant 2
        } else if (cur_x <= 0 && cur_y < 0) {
          child1_x = cur_x - 1;
          child1_y = cur_y; // quadrant 3
        } else {
          child1_x = cur_x;
          child1_y = cur_y - 1; // quadrant 4
        }

        int child2_x = 0, child2_y = 0;
        if (cur_x == 0 && cur_y == 0) {
          child2_x = cur_x;
          child2_y = cur_y; // origin
        } else if (cur_x >= 0 && cur_y > 0) {
          child2_x = cur_x;
          child2_y = cur_y + 1; // quadrant 1
        } else if (cur_x < 0 && cur_y >= 0) {
          child2_x = cur_x - 1;
          child2_y = cur_y; // quadrant 2
        } else if (cur_x <= 0 && cur_y < 0) {
          child2_x = cur_x;
          child2_y = cur_y - 1; // quadrant 3
        } else {
          child2_x = cur_x + 1;
          child2_y = cur_y; // quadrant 4
        }

        test_mark(child1_x, child1_y, min_lit_angle, max_lit_angle, min_angle,
                  outer_angle, &queue_head, &queue_tail);

        if (outer_angle2 != 0) {
          test_mark(child2_x, child2_y, min_lit_angle, max_lit_angle,
                    outer_angle, outer_angle2, &queue_head, &queue_tail);

          int child3_x = 0, child3_y = 0;
          if (cur_x != 0 && cur_y != 0) {
            child3_x = child3_y = 0; // non-axis
          } else if (cur_x > 0) {
            child3_x = cur_x;
            child3_y = cur_y + 1;
          } else if (cur_x < 0) {
            child3_x = cur_x;
            child3_y = cur_y - 1;
          } else if (cur_y > 0) {
            child3_x = cur_x - 1;
            child3_y = cur_y;
          } else if (cur_y < 0) {
            child3_x = cur_x + 1;
            child3_y = cur_y;
          } else {
            child3_x = child3_y = 0; // origin
          }

          test_mark(child3_x, child3_y, min_lit_angle, max_lit_angle,
                    outer_angle2, max_angle, &queue_head, &queue_tail);
        } else {
          test_mark(child2_x, child2_y, min_lit_angle, max_lit_angle,
                    outer_angle, max_angle, &queue_head, &queue_tail);
        }
      } else if (min_lit_angle == min_angle) {
        // Cell is opaque - pass infinitely narrow ray to first corner
        int child1_x = 0, child1_y = 0;
        if (cur_x == 0 && cur_y == 0) {
          child1_x = cur_x;
          child1_y = cur_y; // origin
        } else if (cur_x >= 0 && cur_y > 0) {
          child1_x = cur_x + 1;
          child1_y = cur_y; // quadrant 1
        } else if (cur_x < 0 && cur_y >= 0) {
          child1_x = cur_x;
          child1_y = cur_y + 1; // quadrant 2
        } else if (cur_x <= 0 && cur_y < 0) {
          child1_x = cur_x - 1;
          child1_y = cur_y; // quadrant 3
        } else {
          child1_x = cur_x;
          child1_y = cur_y - 1; // quadrant 4
        }

        mark(child1_x, child1_y, min_angle, min_angle, &queue_head,
             &queue_tail);
      }
    }
  }
}

// Add light to a tile and enqueue it if not already queued
static void mark(int x, int y, int min, int max, int *queue_head_ptr,
                 int *queue_tail_ptr) {
  int table_index = calc_table_index(x, y);
  int min_lit = min_lit_table[table_index];
  int max_lit = max_lit_table[table_index];

  if (min_lit == 0 && max_lit == 0) {
    // No light - not in queue, so add it
    queue_x[*queue_tail_ptr] = x;
    queue_y[*queue_tail_ptr] = y;
    *queue_tail_ptr = (*queue_tail_ptr + 1) % QUEUE_LENGTH;

    min_lit_table[table_index] = min;
    max_lit_table[table_index] = max;
  } else {
    // Already in queue - expand lighting range
    if (min < min_lit) {
      min_lit_table[table_index] = min;
    }
    if (max > max_lit) {
      max_lit_table[table_index] = max;
    }
  }
}

// Test if we can add light to a tile
static void test_mark(int x, int y, int min_lit_angle, int max_lit_angle,
                      int min_angle, int max_angle, int *queue_head_ptr,
                      int *queue_tail_ptr) {
  if (min_lit_angle > max_lit_angle) {
    // Passing light along anomaly axis
    mark(x, y, min_angle, max_angle, queue_head_ptr, queue_tail_ptr);
  } else if (max_angle < min_lit_angle || min_angle > max_lit_angle) {
    // Lightable area is outside the lighting
    return;
  } else if (min_angle <= min_lit_angle && max_lit_angle <= max_angle) {
    // Lightable area contains the lighting
    mark(x, y, min_lit_angle, max_lit_angle, queue_head_ptr, queue_tail_ptr);
  } else if (min_angle >= min_lit_angle && max_lit_angle >= max_angle) {
    // Lightable area contained by the lighting
    mark(x, y, min_angle, max_angle, queue_head_ptr, queue_tail_ptr);
  } else if (min_angle >= min_lit_angle && max_lit_angle <= max_angle) {
    // Least of lightable area overlaps greatest of lighting
    mark(x, y, min_angle, max_lit_angle, queue_head_ptr, queue_tail_ptr);
  } else if (min_angle <= min_lit_angle && max_lit_angle >= max_angle) {
    // Greatest of lightable area overlaps least of lighting
    mark(x, y, min_lit_angle, max_angle, queue_head_ptr, queue_tail_ptr);
  }
}
