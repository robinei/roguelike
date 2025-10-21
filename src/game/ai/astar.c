#include "astar.h"

#define OPEN_SET_MAX 8192

#define SCORE_MAX ((1 << 26) - 1)
#define SCORE_MIN (-(1 << 26))

// Priority queue node for A* open set
typedef struct {
  uint16_t x;
  uint16_t y;
  int32_t f_score; // g + h (can be negative with cost bonuses)
} PQNode;

// Simple binary heap priority queue
typedef struct {
  int count;
  PQNode nodes[OPEN_SET_MAX]; // Reasonable max for open set
} PriorityQueue;

// A* cell state
typedef struct {
  int32_t g_score : 27;    // Cost from start
  uint32_t parent_dir : 2; // Direction to parent (0=N, 1=E, 2=S, 3=W, or
                           // invalid if no parent)
  uint32_t has_parent : 1; // Whether parent_dir points to the parent
  uint32_t in_open : 1;
  uint32_t in_closed : 1;
} AStarCell;

static bool push_node(PriorityQueue *pq, int x, int y, int f_score) {
  if (pq->count >= OPEN_SET_MAX) {
    return false; // Queue full
  }

  // Insert at end
  int i = pq->count++;
  pq->nodes[i] = (PQNode){x, y, f_score};

  // Bubble up
  while (i > 0) {
    int parent = (i - 1) / 2;
    if (pq->nodes[i].f_score >= pq->nodes[parent].f_score) {
      break;
    }
    // Swap
    PQNode tmp = pq->nodes[i];
    pq->nodes[i] = pq->nodes[parent];
    pq->nodes[parent] = tmp;
    i = parent;
  }

  return true; // Successfully added
}

static PQNode pop_node(PriorityQueue *pq) {
  PQNode result = pq->nodes[0];

  // Move last to front
  pq->count--;
  if (pq->count > 0) {
    pq->nodes[0] = pq->nodes[pq->count];

    // Bubble down
    int i = 0;
    while (true) {
      int left = 2 * i + 1;
      int right = 2 * i + 2;
      int smallest = i;

      if (left < pq->count &&
          pq->nodes[left].f_score < pq->nodes[smallest].f_score) {
        smallest = left;
      }
      if (right < pq->count &&
          pq->nodes[right].f_score < pq->nodes[smallest].f_score) {
        smallest = right;
      }

      if (smallest == i) {
        break;
      }

      // Swap
      PQNode tmp = pq->nodes[i];
      pq->nodes[i] = pq->nodes[smallest];
      pq->nodes[smallest] = tmp;
      i = smallest;
    }
  }

  return result;
}

// Manhattan distance heuristic with perpendicular distance tie-breaker
// Keeps paths close to the straight line from start to goal
static int heuristic(int sx, int sy, int cx, int cy, int tx, int ty) {
  // Manhattan distance (admissible for 4-directional movement)
  int dx = cx > tx ? cx - tx : tx - cx;
  int dy = cy > ty ? cy - ty : ty - cy;
  int manhattan = dx + dy;

  // Perpendicular distance tie-breaker: penalize cells far from straight line
  // Uses 2D cross product (z-component of 3D cross product) to measure
  // signed distance from the line connecting start to goal.
  // Formula: (cx - tx) * (sy - ty) - (sx - tx) * (cy - ty)
  // This is the signed area of the parallelogram, proportional to distance from
  // line.
  int dx1 = cx - tx;
  int dy1 = cy - ty;
  int dx2 = sx - tx;
  int dy2 = sy - ty;
  int perp_dist = dx1 * dy2 - dx2 * dy1;
  if (perp_dist < 0) {
    perp_dist = -perp_dist;
  }

  // Add tiny perpendicular distance penalty
  // Cells closer to the straight line get lower f-scores
  return manhattan * 10 + perp_dist / 100;
}

// Reconstruct path from parent directions
static int reconstruct_path(AStarCell *cells, int sx, int sy, int tx, int ty,
                            Direction *moves_out) {
  // Build path backwards directly into moves_out
  int path_len = 0;
  int x = tx;
  int y = ty;

  // Trace back to start, storing in reverse order
  while (!(x == sx && y == sy)) {
    if (path_len >= ASTAR_PATH_MAX_LENGTH) {
      return -1; // Path too long
    }

    AStarCell *cell = &cells[y * MAP_WIDTH_MAX + x];
    assert(cell->has_parent);

    // Get direction from current to parent
    Direction to_parent = cell->parent_dir;

    // Store opposite direction (from parent to current)
    moves_out[path_len++] = dir_opposite(to_parent);
    x += dir_dx(to_parent);
    y += dir_dy(to_parent);
  }

  // Reverse moves_out in-place to get forward order
  for (int i = 0; i < path_len / 2; i++) {
    Direction tmp = moves_out[i];
    moves_out[i] = moves_out[path_len - i - 1];
    moves_out[path_len - i - 1] = tmp;
  }

  return path_len;
}

int astar_find_path(void *ctx, AStarCostFunction cost_func, int map_width,
                    int map_height, int sx, int sy, int tx, int ty,
                    Direction *moves_out) {
  // Validate inputs
  if (sx < 0 || sy < 0 || tx < 0 || ty < 0) {
    return -1;
  }
  if (sx >= map_width || sy >= map_height || tx >= map_width ||
      ty >= map_height) {
    return -1;
  }

  // Already at target?
  if (sx == tx && sy == ty) {
    return 0;
  }

  // Allocate cell state (using MAP_WIDTH_MAX as stride for simplicity)
  // Note: Static buffer makes this function non-reentrant (not thread-safe)
  static AStarCell cells[MAP_WIDTH_MAX * MAP_HEIGHT_MAX];

  // Initialize all cells to unvisited state
  // All fields zeroed: g_score=0, in_open=0, in_closed=0, has_parent=0
  // g_score is only read for cells in open set, which have valid costs
  memset(cells, 0, sizeof(cells));

  PriorityQueue open_set = {0};

  // Initialize start cell
  int start_idx = sy * MAP_WIDTH_MAX + sx;
  cells[start_idx].g_score = 0;
  cells[start_idx].in_open = 1;
  cells[start_idx].has_parent = 0;

  int f_start = heuristic(sx, sy, sx, sy, tx, ty);
  bool initial_push_ok = push_node(&open_set, sx, sy, f_start);
  assert(initial_push_ok);

  int popped_count = 0;
  int pushed_count = 1;

  // A* main loop
  while (open_set.count) {
    PQNode node = pop_node(&open_set);
    int cx = node.x;
    int cy = node.y;
    AStarCell *current = &cells[cy * MAP_WIDTH_MAX + cx];
    ++popped_count;

    // Skip if already processed (stale duplicate from queue)
    if (current->in_closed) {
      continue;
    }

    // Mark as closed
    current->in_open = 0;
    current->in_closed = 1;

    // Reached goal?
    if (cx == tx && cy == ty) {
      // output_message("pushed_count: %d, popped_count: %d", pushed_count,
      //                popped_count);
      return reconstruct_path(cells, sx, sy, tx, ty, moves_out);
    }

    // Check 4 neighbors (N, E, S, W)
    for (Direction dir = 0; dir < 4; dir++) {
      int nx = cx + dir_dx(dir);
      int ny = cy + dir_dy(dir);
      if (nx < 0 || ny < 0 || nx >= map_width || ny >= map_height) {
        continue;
      }

      AStarCell *neighbor = &cells[ny * MAP_WIDTH_MAX + nx];
      if (neighbor->in_closed) {
        continue;
      }

      int move_cost = cost_func(ctx, cx, cy, nx, ny);
      if (move_cost == ASTAR_COST_INFINITE) {
        continue; // Impassable coordinate
      }

      int tentative_g = current->g_score + move_cost;
      if (tentative_g > SCORE_MAX || tentative_g < SCORE_MIN) {
        continue; // cost out of range for 27 bit open set storage
      }

      // If not in open set, or found better path
      if (!neighbor->in_open || tentative_g < neighbor->g_score) {
        neighbor->g_score = tentative_g;
        neighbor->parent_dir =
            dir_opposite(dir); // Direction from neighbor back to current
        neighbor->has_parent = 1;

        int f_score = tentative_g + heuristic(sx, sy, nx, ny, tx, ty);

        // PQNode Push to queue (even if already in open set with worse f_score)
        // We don't have decrease-key, so we push duplicates
        // The closed set check prevents processing the same node twice
        if (push_node(&open_set, nx, ny, f_score)) {
          neighbor->in_open = 1;
          ++pushed_count;
        }
        // If push fails (queue full), we skip this cell
        // This degrades gracefully: we may miss optimal path but won't corrupt
        // state
      }
    }
  }

  // No path found
  return -1;
}
