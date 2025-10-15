#include "flood.h"
#include "common.h"
#include "map.h"

// Simulation parameters
#define DAMPING 0.98f        // Velocity damping for stability
#define DEPTH_TRANSFER 0.25f // How much depth difference drives flow
#define VELOCITY_SCALE 0.5f  // Scale down velocities to prevent instability
#define MAX_VELOCITY 20.0f   // Maximum velocity magnitude to prevent runaway

#define IX(x, y) ((y) * MAP_WIDTH_MAX + (x))

// Temporary buffer for double-buffering
static float temp_depth[MAP_WIDTH_MAX * MAP_HEIGHT_MAX];

// Helper to check if a cell is solid (impassable)
static inline bool is_solid(Map *map, int x, int y) {
  return !map->cells[IX(x, y)].passable;
}

// Helper to get depth at position, treating outside map as max depth
static inline float get_neighbour_depth(Map *map, int x, int y,
                                        float self_depth) {
  if (x < 0 || x >= map->width || y < 0 || y >= map->height) {
    return 1.0f; // Cells outside map are considered full of water
  }
  if (is_solid(map, x, y)) {
    return self_depth; // Block flows through solid walls
  }
  return map->water_depth[IX(x, y)];
}

void flood_simulate_step(Map *map) {
  int width = map->width;
  int height = map->height;

  // Step 0: Apply boundary condition
  for (int x = 0; x < width; x++) {
    map->water_depth[IX(x, 0)] = is_solid(map, x, 0) ? 0.0f : 1.0f;
    map->water_depth[IX(x, height - 1)] =
        is_solid(map, x, height - 1) ? 0.0f : 1.0f;
  }
  for (int y = 0; y < height; y++) {
    map->water_depth[IX(0, y)] = is_solid(map, 0, y) ? 0.0f : 1.0f;
    map->water_depth[IX(width - 1, y)] =
        is_solid(map, width - 1, y) ? 0.0f : 1.0f;
  }

  // Step 1: Update velocities based on depth gradients
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      int idx = IX(x, y);

      // Skip solid cells
      if (is_solid(map, x, y)) {
        map->water_velx[idx] = 0;
        map->water_vely[idx] = 0;
        continue;
      }

      // Get current depth and velocity
      float depth = map->water_depth[idx];
      float vx = map->water_velx[idx];
      float vy = map->water_vely[idx];

      // Calculate depth gradients (pressure gradients)
      // Check all 4 cardinal directions
      float depth_left = get_neighbour_depth(map, x - 1, y, depth);
      float depth_right = get_neighbour_depth(map, x + 1, y, depth);
      float depth_up = get_neighbour_depth(map, x, y - 1, depth);
      float depth_down = get_neighbour_depth(map, x, y + 1, depth);

      // Pressure gradient acceleration (water flows from high to low depth)
      float grad_x = (depth_left - depth_right) * DEPTH_TRANSFER;
      float grad_y = (depth_up - depth_down) * DEPTH_TRANSFER;

      // Update velocity with damping and clamping
      vx = (vx + grad_x) * DAMPING;
      vy = (vy + grad_y) * DAMPING;
      map->water_velx[idx] = clamp_float(vx, -MAX_VELOCITY, MAX_VELOCITY);
      map->water_vely[idx] = clamp_float(vy, -MAX_VELOCITY, MAX_VELOCITY);
    }
  }

  // Step 2: Advect depth based on velocities
  // Use temp buffer to avoid read/write conflicts
  memset(temp_depth, 0, sizeof(temp_depth));

  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      int idx = IX(x, y);

      // Skip solid cells
      if (is_solid(map, x, y)) {
        continue;
      }

      float depth = map->water_depth[idx];
      if (depth <= 0) {
        continue;
      }

      float vx = map->water_velx[idx] * VELOCITY_SCALE;
      float vy = map->water_vely[idx] * VELOCITY_SCALE;

      // Calculate how much water flows in each direction
      // Positive velocity means flow to the right/down
      float flow_right = vx > 0 ? vx : 0;
      float flow_left = vx < 0 ? -vx : 0;
      float flow_down = vy > 0 ? vy : 0;
      float flow_up = vy < 0 ? -vy : 0;

      float total_flow = flow_right + flow_left + flow_down + flow_up;
      if (total_flow <= 0) {
        temp_depth[idx] += depth; // Nothing flows
        continue;
      }

      // Calculate what fraction stays vs flows
      float max_flow = depth * 0.8f; // Don't let all water leave
      if (total_flow > max_flow) {
        float scale = max_flow / total_flow;
        flow_right *= scale;
        flow_left *= scale;
        flow_down *= scale;
        flow_up *= scale;
        total_flow = max_flow;
      }

      // Check capacity for each neighbor and calculate what can actually flow
      float flows[4] = {flow_right, flow_left, flow_down, flow_up};
      bool can_flow[4];
      float available[4];
      int neighbor_idx[4] = {IX(x + 1, y), IX(x - 1, y), IX(x, y + 1),
                             IX(x, y - 1)};

      // Check right
      can_flow[0] = (x + 1 < width && !is_solid(map, x + 1, y));
      available[0] = can_flow[0] ? (1.0f - temp_depth[neighbor_idx[0]]) : 0.0f;

      // Check left
      can_flow[1] = (x - 1 >= 0 && !is_solid(map, x - 1, y));
      available[1] = can_flow[1] ? (1.0f - temp_depth[neighbor_idx[1]]) : 0.0f;

      // Check down
      can_flow[2] = (y + 1 < height && !is_solid(map, x, y + 1));
      available[2] = can_flow[2] ? (1.0f - temp_depth[neighbor_idx[2]]) : 0.0f;

      // Check up
      can_flow[3] = (y - 1 >= 0 && !is_solid(map, x, y - 1));
      available[3] = can_flow[3] ? (1.0f - temp_depth[neighbor_idx[3]]) : 0.0f;

      // Redistribute blocked flows
      for (int i = 0; i < 4; i++) {
        if (!can_flow[i] || flows[i] >= available[i]) {
          // This flow is blocked or would overflow
          float blocked = can_flow[i] ? (flows[i] - available[i]) : flows[i];
          flows[i] = can_flow[i] ? available[i] : 0.0f;

          // Redistribute blocked amount among other directions + self
          // Count how many valid redistribution targets (other dirs + self =
          // always at least 1)
          int targets = 1; // Always include self
          for (int j = 0; j < 4; j++) {
            if (j != i && can_flow[j])
              targets++;
          }

          float share = blocked / targets;

          // Add share to self (staying water)
          temp_depth[idx] += share;

          // Add share to other directions
          for (int j = 0; j < 4; j++) {
            if (j != i && can_flow[j]) {
              flows[j] += share;
            }
          }
        }
      }

      // Apply final flows
      temp_depth[idx] += depth - total_flow; // Base staying water
      for (int i = 0; i < 4; i++) {
        if (can_flow[i] && flows[i] > 0) {
          temp_depth[neighbor_idx[i]] += flows[i];
        }
      }
    }
  }

  // Copy depth back
  memcpy(map->water_depth, temp_depth, sizeof(temp_depth));
}
