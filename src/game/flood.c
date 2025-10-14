#include "common.h"
#include "flood.h"

// Simulation parameters
#define PRESSURE_CONSTANT 0.8f
#define DAMPING 0.92f
#define MAX_VELOCITY 80

// Helper to get water cell at position
static inline WaterCell *get_water(Map *map, int x, int y) {
  if (x < 0 || x >= map->width || y < 0 || y >= map->height) {
    return NULL;
  }
  return &map->water[y * MAP_WIDTH_MAX + x];
}

// Helper to get map cell at position
static inline MapCell *get_cell(Map *map, int x, int y) {
  if (x < 0 || x >= map->width || y < 0 || y >= map->height) {
    return NULL;
  }
  return &map->cells[y * MAP_WIDTH_MAX + x];
}

void flood_simulate_step(Map *map) {
  // Temp buffers for clean double-buffering
  static int8_t temp_vel_x[MAP_WIDTH_MAX * MAP_HEIGHT_MAX];
  static int8_t temp_vel_y[MAP_WIDTH_MAX * MAP_HEIGHT_MAX];
  static uint8_t temp_depth[MAP_WIDTH_MAX * MAP_HEIGHT_MAX];

  // PASS 1: Update velocities based on pressure gradients
  // Input: map->water[].depth (read only)
  // Output: temp_vel[] (write only)
  for (int y = 0; y < map->height; y++) {
    for (int x = 0; x < map->width; x++) {
      int idx = y * MAP_WIDTH_MAX + x;
      MapCell *cell = get_cell(map, x, y);

      if (!cell->passable) {
        temp_vel_x[idx] = 0;
        temp_vel_y[idx] = 0;
        continue;
      }

      WaterCell *water = get_water(map, x, y);
      int my_depth = water->water_depth;

      // Calculate pressure gradients in each direction
      int depth_left = my_depth;
      int depth_right = my_depth;
      int depth_up = my_depth;
      int depth_down = my_depth;

      // Left neighbor
      WaterCell *w_left = get_water(map, x - 1, y);
      MapCell *c_left = get_cell(map, x - 1, y);
      if (!w_left) {
        depth_left = 255; // Off-map
      } else if (c_left->passable) {
        depth_left = w_left->water_depth;
      }

      // Right neighbor
      WaterCell *w_right = get_water(map, x + 1, y);
      MapCell *c_right = get_cell(map, x + 1, y);
      if (!w_right) {
        depth_right = 255; // Off-map
      } else if (c_right->passable) {
        depth_right = w_right->water_depth;
      }

      // Up neighbor
      WaterCell *w_up = get_water(map, x, y - 1);
      MapCell *c_up = get_cell(map, x, y - 1);
      if (!w_up) {
        depth_up = 255; // Off-map
      } else if (c_up->passable) {
        depth_up = w_up->water_depth;
      }

      // Down neighbor
      WaterCell *w_down = get_water(map, x, y + 1);
      MapCell *c_down = get_cell(map, x, y + 1);
      if (!w_down) {
        depth_down = 255; // Off-map
      } else if (c_down->passable) {
        depth_down = w_down->water_depth;
      }

      // Pressure gradient creates acceleration
      // Higher depth on left pushes right, higher on right pushes left
      int pressure_x = depth_left - depth_right;
      int pressure_y = depth_up - depth_down;

      int accel_x = (int)(pressure_x * PRESSURE_CONSTANT);
      int accel_y = (int)(pressure_y * PRESSURE_CONSTANT);

      // Update velocity
      int new_vx = water->velocity_x + accel_x;
      int new_vy = water->velocity_y + accel_y;

      // Apply damping
      new_vx = (int)(new_vx * DAMPING);
      new_vy = (int)(new_vy * DAMPING);

      // Clamp velocity
      temp_vel_x[idx] = clamp_int(new_vx, -MAX_VELOCITY, MAX_VELOCITY);
      temp_vel_y[idx] = clamp_int(new_vy, -MAX_VELOCITY, MAX_VELOCITY);
    }
  }

  // PASS 2: Transport water based on velocity
  // Input: map->water[].depth (read only), temp_vel[] (read only)
  // Output: temp_depth[] (write only)

  // Initialize temp_depth from current state
  for (int i = 0; i < MAP_WIDTH_MAX * MAP_HEIGHT_MAX; i++) {
    temp_depth[i] = map->water[i].water_depth;
  }

  // Process each edge exactly once (right and down only)
  for (int y = 0; y < map->height; y++) {
    for (int x = 0; x < map->width; x++) {
      int idx = y * MAP_WIDTH_MAX + x;
      MapCell *cell = get_cell(map, x, y);

      if (!cell->passable) {
        continue;
      }

      // Process RIGHT edge (between x and x+1)
      WaterCell *w_right = get_water(map, x + 1, y);
      MapCell *c_right = get_cell(map, x + 1, y);

      if (w_right && c_right->passable) {
        // Both cells exist and are passable - calculate flow
        int idx_right = y * MAP_WIDTH_MAX + (x + 1);

        // Average velocity at the edge
        int v_avg = (temp_vel_x[idx] + temp_vel_x[idx_right]) / 2;

        if (v_avg > 0) {
          // Flow from left to right
          int depth_source = map->water[idx].water_depth;
          if (depth_source > 0) {
            int transfer = (v_avg * depth_source) / 127;
            transfer = clamp_int(transfer, 0, depth_source / 2);
            temp_depth[idx] -= transfer;
            temp_depth[idx_right] += transfer;
          }
        } else if (v_avg < 0) {
          // Flow from right to left
          int depth_source = map->water[idx_right].water_depth;
          if (depth_source > 0) {
            int transfer = (-v_avg * depth_source) / 127;
            transfer = clamp_int(transfer, 0, depth_source / 2);
            temp_depth[idx_right] -= transfer;
            temp_depth[idx] += transfer;
          }
        }
      } else if (!w_right) {
        // Right edge of map - check for inflow from off-map
        // Velocity pointing left (negative) means pressure from off-map pushing
        // in
        if (temp_vel_x[idx] < 0) {
          int inflow = (-temp_vel_x[idx] * 2);
          inflow = clamp_int(inflow, 0, 50);
          temp_depth[idx] = clamp_int(temp_depth[idx] + inflow, 0, 255);
        }
      }

      // Process DOWN edge (between y and y+1)
      WaterCell *w_down = get_water(map, x, y + 1);
      MapCell *c_down = get_cell(map, x, y + 1);

      if (w_down && c_down->passable) {
        // Both cells exist and are passable - calculate flow
        int idx_down = (y + 1) * MAP_WIDTH_MAX + x;

        // Average velocity at the edge
        int v_avg = (temp_vel_y[idx] + temp_vel_y[idx_down]) / 2;

        if (v_avg > 0) {
          // Flow from up to down
          int depth_source = map->water[idx].water_depth;
          if (depth_source > 0) {
            int transfer = (v_avg * depth_source) / 127;
            transfer = clamp_int(transfer, 0, depth_source / 2);
            temp_depth[idx] -= transfer;
            temp_depth[idx_down] += transfer;
          }
        } else if (v_avg < 0) {
          // Flow from down to up
          int depth_source = map->water[idx_down].water_depth;
          if (depth_source > 0) {
            int transfer = (-v_avg * depth_source) / 127;
            transfer = clamp_int(transfer, 0, depth_source / 2);
            temp_depth[idx_down] -= transfer;
            temp_depth[idx] += transfer;
          }
        }
      } else if (!w_down) {
        // Bottom edge of map - check for inflow from off-map
        // Velocity pointing up (negative) means pressure from off-map pushing
        // in
        if (temp_vel_y[idx] < 0) {
          int inflow = (-temp_vel_y[idx] * 2);
          inflow = clamp_int(inflow, 0, 50);
          temp_depth[idx] = clamp_int(temp_depth[idx] + inflow, 0, 255);
        }
      }
    }
  }

  // Handle left and top edges (which weren't covered by the right/down pass)
  for (int y = 0; y < map->height; y++) {
    for (int x = 0; x < map->width; x++) {
      int idx = y * MAP_WIDTH_MAX + x;
      MapCell *cell = get_cell(map, x, y);

      if (!cell->passable) {
        continue;
      }

      // Left edge (x == 0)
      if (x == 0) {
        WaterCell *w_left = get_water(map, x - 1, y);
        if (!w_left && temp_vel_x[idx] > 0) {
          // Off-map to the left, velocity pointing right (pressure from
          // off-map)
          int inflow = (temp_vel_x[idx] * 2);
          inflow = clamp_int(inflow, 0, 50);
          temp_depth[idx] = clamp_int(temp_depth[idx] + inflow, 0, 255);
        }
      }

      // Top edge (y == 0)
      if (y == 0) {
        WaterCell *w_up = get_water(map, x, y - 1);
        if (!w_up && temp_vel_y[idx] > 0) {
          // Off-map above, velocity pointing down (pressure from off-map)
          int inflow = (temp_vel_y[idx] * 2);
          inflow = clamp_int(inflow, 0, 50);
          temp_depth[idx] = clamp_int(temp_depth[idx] + inflow, 0, 255);
        }
      }
    }
  }

  // Copy temp buffers back to map
  for (int i = 0; i < MAP_WIDTH_MAX * MAP_HEIGHT_MAX; i++) {
    map->water[i].water_depth = temp_depth[i];
    map->water[i].velocity_x = temp_vel_x[i];
    map->water[i].velocity_y = temp_vel_y[i];
  }
}
