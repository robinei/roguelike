#include "../random.h"
#include "../render_api.h"
#include "mapgen.h"

// BSP Tree node representing a rectangular region
typedef struct BSPNode {
  int x, y; // Top-left corner
  int width, height;

  // Room within this region (only in leaf nodes)
  int room_x, room_y;
  int room_width, room_height;

  // Children (NULL for leaf nodes)
  struct BSPNode *left;
  struct BSPNode *right;

  // Split information (for corridor generation)
  bool split_horizontal;
  int split_pos; // Position of split in parent coordinates
} BSPNode;

#define MAX_NODES 256
static BSPNode node_pool[MAX_NODES];
static int node_count;

// Allocate a new node from the pool
static BSPNode *alloc_node(int x, int y, int width, int height) {
  if (node_count >= MAX_NODES)
    return NULL;

  BSPNode *node = &node_pool[node_count++];
  memset(node, 0, sizeof(BSPNode));
  node->x = x;
  node->y = y;
  node->width = width;
  node->height = height;
  return node;
}

// Recursively split a region into a BSP tree
static void split_region(BSPNode *node, int depth, const BSPGenParams *params) {
  // Stop splitting if we've reached max depth or region is too small
  if (depth >= params->max_depth || node->width < params->min_region_size ||
      node->height < params->min_region_size) {
    return; // This is now a leaf node
  }

  // Decide split direction (prefer splitting along the longer axis)
  bool split_horizontal;
  if (node->width > node->height && node->width > params->split_threshold) {
    split_horizontal = false; // Split vertically
  } else if (node->height > node->width &&
             node->height > params->split_threshold) {
    split_horizontal = true; // Split horizontally
  } else {
    split_horizontal = (random64() % 2) == 0;
  }

  node->split_horizontal = split_horizontal;

  // Choose split position (avoid edges, leave room for rooms and corridors)
  int split_pos;

  if (split_horizontal) {
    // Split horizontally (top/bottom)
    int max_split = node->height - params->min_child_size;
    if (max_split <= params->min_child_size)
      return; // Can't split
    split_pos =
        params->min_child_size + (random64() % (max_split - params->min_child_size));
  } else {
    // Split vertically (left/right)
    int max_split = node->width - params->min_child_size;
    if (max_split <= params->min_child_size)
      return; // Can't split
    split_pos =
        params->min_child_size + (random64() % (max_split - params->min_child_size));
  }

  node->split_pos = split_pos;

  // Create child nodes
  if (split_horizontal) {
    node->left = alloc_node(node->x, node->y, node->width, split_pos);
    node->right = alloc_node(node->x, node->y + split_pos, node->width,
                             node->height - split_pos);
  } else {
    node->left = alloc_node(node->x, node->y, split_pos, node->height);
    node->right = alloc_node(node->x + split_pos, node->y,
                             node->width - split_pos, node->height);
  }

  // Recursively split children
  if (node->left)
    split_region(node->left, depth + 1, params);
  if (node->right)
    split_region(node->right, depth + 1, params);
}

// Create a room in a leaf node
static void create_room(BSPNode *node, const BSPGenParams *params) {
  // Don't create rooms in non-leaf nodes
  if (node->left || node->right)
    return;

  // Room should be smaller than the region, with some padding
  int padding = params->room_padding;
  int max_width = node->width - padding * 2;
  int max_height = node->height - padding * 2;

  if (max_width < params->min_room_size || max_height < params->min_room_size) {
    // Region too small for a room
    node->room_width = 0;
    node->room_height = 0;
    return;
  }

  // Random room size
  node->room_width =
      params->min_room_size + (random64() % (max_width - params->min_room_size + 1));
  node->room_height =
      params->min_room_size + (random64() % (max_height - params->min_room_size + 1));

  // Random position within the region (with padding)
  int max_x = node->width - node->room_width - padding;
  int max_y = node->height - node->room_height - padding;

  node->room_x = node->x + padding + (random64() % (max_x + 1));
  node->room_y = node->y + padding + (random64() % (max_y + 1));
}

// Get the center point of a room in a subtree (used for corridor connections)
static bool get_center(BSPNode *node, int *center_x, int *center_y) {
  if (!node)
    return false;

  // If this is a leaf with a room, return its center
  if (!node->left && !node->right && node->room_width > 0) {
    *center_x = node->room_x + node->room_width / 2;
    *center_y = node->room_y + node->room_height / 2;
    return true;
  }

  // If this is a non-leaf, use the center of the left child's room
  if (node->left && get_center(node->left, center_x, center_y)) {
    return true;
  }

  // Fall back to right child
  if (node->right && get_center(node->right, center_x, center_y)) {
    return true;
  }

  return false;
}

// Draw a horizontal corridor
static void draw_h_corridor(Map *map, int x1, int x2, int y) {
  if (y < 0 || y >= map->height)
    return;

  int start = x1 < x2 ? x1 : x2;
  int end = x1 < x2 ? x2 : x1;

  for (int x = start; x <= end; x++) {
    if (x >= 0 && x < map->width) {
      map->cells[y * MAP_WIDTH_MAX + x].passable = 1;
      map->cells[y * MAP_WIDTH_MAX + x].tile = TILE_FLOOR;
    }
  }
}

// Draw a vertical corridor
static void draw_v_corridor(Map *map, int y1, int y2, int x) {
  if (x < 0 || x >= map->width)
    return;

  int start = y1 < y2 ? y1 : y2;
  int end = y1 < y2 ? y2 : y1;

  for (int y = start; y <= end; y++) {
    if (y >= 0 && y < map->height) {
      map->cells[y * MAP_WIDTH_MAX + x].passable = 1;
      map->cells[y * MAP_WIDTH_MAX + x].tile = TILE_FLOOR;
    }
  }
}

// Connect two subtrees with corridors
static void connect_rooms(Map *map, BSPNode *node) {
  if (!node || !node->left || !node->right)
    return;

  // Recursively connect children first
  connect_rooms(map, node->left);
  connect_rooms(map, node->right);

  // Get center points of left and right subtrees
  int left_x, left_y, right_x, right_y;
  if (!get_center(node->left, &left_x, &left_y))
    return;
  if (!get_center(node->right, &right_x, &right_y))
    return;

  // Create L-shaped corridor connecting the two centers
  if (random64() % 2 == 0) {
    // Horizontal then vertical
    draw_h_corridor(map, left_x, right_x, left_y);
    draw_v_corridor(map, left_y, right_y, right_x);
  } else {
    // Vertical then horizontal
    draw_v_corridor(map, left_y, right_y, left_x);
    draw_h_corridor(map, left_x, right_x, right_y);
  }
}

// Fill rooms into the map
static void draw_rooms(Map *map, BSPNode *node) {
  if (!node)
    return;

  // If this is a leaf node with a room, draw it
  if (!node->left && !node->right && node->room_width > 0) {
    for (int y = 0; y < node->room_height; y++) {
      for (int x = 0; x < node->room_width; x++) {
        int mx = node->room_x + x;
        int my = node->room_y + y;
        if (mx >= 0 && mx < map->width && my >= 0 && my < map->height) {
          map->cells[my * MAP_WIDTH_MAX + mx].passable = 1;
          map->cells[my * MAP_WIDTH_MAX + mx].tile = TILE_FLOOR;
        }
      }
    }
  }

  // Recurse to children
  draw_rooms(map, node->left);
  draw_rooms(map, node->right);
}

void mapgen_bsp(Map *map, const BSPGenParams *params) {
  // Initialize map as all walls
  for (int y = 0; y < map->height; y++) {
    for (int x = 0; x < map->width; x++) {
      map->cells[y * MAP_WIDTH_MAX + x].passable = 0;
      map->cells[y * MAP_WIDTH_MAX + x].tile = TILE_WALL;
    }
  }

  // Reset node pool
  node_count = 0;

  // Create root node with border around the map edge
  int border = params->map_border;
  BSPNode *root = alloc_node(border, border,
                             map->width - border * 2,
                             map->height - border * 2);
  if (!root)
    return;

  // Recursively split the space
  split_region(root, 0, params);

  // Create rooms in leaf nodes
  for (int i = 0; i < node_count; i++) {
    create_room(&node_pool[i], params);
  }

  // Draw rooms to map
  draw_rooms(map, root);

  // Connect rooms with corridors
  connect_rooms(map, root);
}
