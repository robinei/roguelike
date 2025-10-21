#include "../random.h"
#include "mapgen.h"

// Small-scale subsea terrain types
typedef enum {
  TERRAIN_NONE,

  TERRAIN_SEDIMENT,          // Soft muddy/sandy floor (common, passable)
  TERRAIN_ROCK,              // Exposed bedrock
  TERRAIN_BOULDER_FIELD,     // Scattered large rocks
  TERRAIN_VOLCANIC_ROCK,     // Dark basalt
  TERRAIN_HYDROTHERMAL_VENT, // Hot water vents
  TERRAIN_CORAL_GARDEN,      // Dense biological growth
  TERRAIN_KELP,              // Tall plants
  TERRAIN_CREVASSE,          // Narrow deep cracks
  TERRAIN_PILLOW_LAVA,       // Bulbous volcanic formations

  TERRAIN_COUNT
} TerrainType;

// Affinity/reward table: positive = good adjacency, negative = bad, 0 = neutral
// terrain_affinity[A][B] = how much terrain A "likes" being next to terrain B
// Diagonal values (affinity[T][T]) represent clustering strength
static int terrain_affinity[TERRAIN_COUNT][TERRAIN_COUNT];

// Visual tile mapping for each terrain type
static const int terrain_tiles[] = {
    [TERRAIN_NONE] = 10299,             //
    [TERRAIN_SEDIMENT] = 1253,          //
    [TERRAIN_ROCK] = 1254,              //
    [TERRAIN_BOULDER_FIELD] = 1047,     //
    [TERRAIN_VOLCANIC_ROCK] = 1255,     //
    [TERRAIN_HYDROTHERMAL_VENT] = 7014, //
    [TERRAIN_CORAL_GARDEN] = 1046,      //
    [TERRAIN_KELP] = 1045,              //
    [TERRAIN_CREVASSE] = 1442,          //
    [TERRAIN_PILLOW_LAVA] = 1044,       //
};

// Passability for each terrain type
static const bool terrain_passable[] = {
    [TERRAIN_NONE] = true,
    [TERRAIN_SEDIMENT] = true,
    [TERRAIN_ROCK] = true,
    [TERRAIN_BOULDER_FIELD] = true,
    [TERRAIN_VOLCANIC_ROCK] = true,
    [TERRAIN_HYDROTHERMAL_VENT] = false, // Dangerous!
    [TERRAIN_CORAL_GARDEN] = true,       // Slow movement
    [TERRAIN_KELP] = true,               // Slow movement, obscures vision
    [TERRAIN_CREVASSE] = false,          // Impassable
    [TERRAIN_PILLOW_LAVA] = true,        // Difficult terrain
};

// Initial terrain distribution weights (for random initialization)
typedef struct {
  TerrainType terrain;
  int weight; // Relative probability
} TerrainWeight;

// Default weights for subsea generation
static const int default_weights[] = {
    [TERRAIN_SEDIMENT] = 40,         // Very common
    [TERRAIN_ROCK] = 15,             // Common
    [TERRAIN_BOULDER_FIELD] = 10,    // Moderate
    [TERRAIN_VOLCANIC_ROCK] = 8,     // Less common
    [TERRAIN_HYDROTHERMAL_VENT] = 3, // Rare
    [TERRAIN_CORAL_GARDEN] = 8,      // Moderate
    [TERRAIN_KELP] = 10,             // Moderate
    [TERRAIN_CREVASSE] = 3,          // Rare
    [TERRAIN_PILLOW_LAVA] = 5,       // Less common
};

// Initialize terrain affinity/reward rules
static void init_terrain_affinity(void) {
  // Start with all zero (neutral affinity)
  memset(terrain_affinity, 0, sizeof(terrain_affinity));

// Helper macro to set affinity symmetrically
#define AFFINITY(a, b, score)                                                  \
  do {                                                                         \
    terrain_affinity[TERRAIN_##a][TERRAIN_##b] = (score);                      \
    terrain_affinity[TERRAIN_##b][TERRAIN_##a] = (score);                      \
  } while (0)

  // Self-affinity (clustering rewards) - diagonal of the matrix
  // All equal to prevent any single terrain from dominating
  AFFINITY(SEDIMENT, SEDIMENT, 2);
  AFFINITY(ROCK, ROCK, 2);
  AFFINITY(BOULDER_FIELD, BOULDER_FIELD, 2);
  AFFINITY(VOLCANIC_ROCK, VOLCANIC_ROCK, 2);
  AFFINITY(HYDROTHERMAL_VENT, HYDROTHERMAL_VENT, 2);
  AFFINITY(CORAL_GARDEN, CORAL_GARDEN, 2);
  AFFINITY(KELP, KELP, 2);
  AFFINITY(CREVASSE, CREVASSE, 2);
  AFFINITY(PILLOW_LAVA, PILLOW_LAVA, 2);

  // Hard conflicts (large negative penalties)
  AFFINITY(SEDIMENT, VOLCANIC_ROCK, -10);     // Volcanic areas = rocky
  AFFINITY(SEDIMENT, PILLOW_LAVA, -10);       // Lava flows = rocky
  AFFINITY(SEDIMENT, HYDROTHERMAL_VENT, -10); // Vents in rocky areas only

  AFFINITY(ROCK, VOLCANIC_ROCK, -10); // Different rock types don't mix
  AFFINITY(ROCK, PILLOW_LAVA, -10);   // Different formations

  AFFINITY(BOULDER_FIELD, VOLCANIC_ROCK, -10); // Boulders from normal rock
  AFFINITY(BOULDER_FIELD, PILLOW_LAVA, -10);

  // Hydrothermal vent conflicts (only near volcanic features)
  AFFINITY(HYDROTHERMAL_VENT, SEDIMENT, -10);
  AFFINITY(HYDROTHERMAL_VENT, ROCK, -10);
  AFFINITY(HYDROTHERMAL_VENT, BOULDER_FIELD, -10);
  AFFINITY(HYDROTHERMAL_VENT, CORAL_GARDEN, -10); // Too hot for life
  AFFINITY(HYDROTHERMAL_VENT, KELP, -10);

  // Biological conflicts
  AFFINITY(KELP, VOLCANIC_ROCK, -10); // Kelp prefers sediment
  AFFINITY(KELP, PILLOW_LAVA, -10);   // Not on lava
  AFFINITY(KELP, CREVASSE, -10);      // Can't grow in cracks

  AFFINITY(CORAL_GARDEN, SEDIMENT, -10);      // Corals need hard substrate
  AFFINITY(CORAL_GARDEN, VOLCANIC_ROCK, -10); // Prefers normal rock
  AFFINITY(CORAL_GARDEN, PILLOW_LAVA, -10);
  AFFINITY(CORAL_GARDEN, CREVASSE, -10);

  // Crevasse conflicts (only in hard rock)
  AFFINITY(CREVASSE, SEDIMENT, -10);
  AFFINITY(CREVASSE, BOULDER_FIELD, -10);
  AFFINITY(CREVASSE, VOLCANIC_ROCK, -10);
  AFFINITY(CREVASSE, PILLOW_LAVA, -10);

  // Soft positive affinities (natural transitions and combinations)
  AFFINITY(VOLCANIC_ROCK, PILLOW_LAVA, 3);       // Volcanic features together
  AFFINITY(VOLCANIC_ROCK, HYDROTHERMAL_VENT, 2); // Vents near volcanic rock
  AFFINITY(PILLOW_LAVA, HYDROTHERMAL_VENT, 2);   // Vents near lava

  AFFINITY(SEDIMENT, ROCK, 1);              // Natural sediment-rock transition
  AFFINITY(KELP, SEDIMENT, 2);              // Kelp grows on sediment
  AFFINITY(CORAL_GARDEN, ROCK, 2);          // Coral grows on rock
  AFFINITY(CORAL_GARDEN, BOULDER_FIELD, 2); // Coral around boulders

#undef AFFINITY
}

// Score a terrain choice at position (x, y) based on neighbor affinities
// Higher score = better placement
// Works directly on map coordinates, samples anywhere within map bounds
static int score_terrain(Map *map, int x, int y, TerrainType terrain,
                         int radius) {
  int score = 0;
  int count = 0;

  // Check all neighbors within radius
  for (int dy = -radius; dy <= radius; dy++) {
    for (int dx = -radius; dx <= radius; dx++) {
      if (dx == 0 && dy == 0) {
        continue; // Skip self
      }

      int nx = x + dx;
      int ny = y + dy;

      // Check bounds (avoid buffer overrun)
      if (nx < 0 || nx >= map->width || ny < 0 || ny >= map->height) {
        continue; // Out of bounds, skip
      }

      int map_idx = ny * MAP_WIDTH_MAX + nx;
      TerrainType neighbor = (TerrainType)map->cells[map_idx].category;

      // Skip ungenerated tiles (category 0 is unused/ungenerated)
      if (neighbor == 0) {
        continue; // Treat as unknown, don't constrain against it
      }

      // Add affinity score (positive for good adjacency, negative for bad)
      score += terrain_affinity[terrain][neighbor];
      ++count;
    }
  }

  return count > 0 ? (score * (4 * radius * radius - 1)) / count : score;
}

// Generate random terrain according to weighted distribution
static TerrainType random_weighted_terrain(const int *weights, int count) {
  int total_weight = 0;
  for (int i = 0; i < count; i++) {
    total_weight += weights[i];
  }

  int roll = random64() % total_weight;
  int cumulative = 0;

  for (int i = 0; i < count; i++) {
    cumulative += weights[i];
    if (roll < cumulative) {
      return i;
    }
  }

  return 0; // Fallback
}

// Generate terrain using Constraint Satisfaction with Local Minimum Conflicts
// Works directly on the map's category field within the specified region
void mapgen_csp_region(Map *map, int region_x, int region_y, int region_width,
                       int region_height, const CSPGenParams *params) {
  // Validate region bounds
  if (region_x < 0 || region_y < 0 || region_x + region_width > map->width ||
      region_y + region_height > map->height) {
    return; // Invalid region
  }

  // Initialize affinity rules (only once)
  static bool affinity_initialized = false;
  if (!affinity_initialized) {
    init_terrain_affinity();
    affinity_initialized = true;
  }

  // Step 1: Random initialization - write directly to map.category
  for (int y = region_y; y < region_y + region_height; y++) {
    for (int x = region_x; x < region_x + region_width; x++) {
      int map_idx = y * MAP_WIDTH_MAX + x;
      TerrainType terrain =
          random_weighted_terrain(default_weights, TERRAIN_COUNT);
      map->cells[map_idx].category = terrain;
    }
  }

  // Step 2: Iterative refinement - maximize affinity score
  const int check_radius = 2; // How far to check for neighbor affinities

  for (int iter = 0; iter < params->iterations; iter++) {
    // Pick a random tile within the region
    int x = region_x + (random64() % region_width);
    int y = region_y + (random64() % region_height);
    int map_idx = y * MAP_WIDTH_MAX + x;

    TerrainType current = (TerrainType)map->cells[map_idx].category;
    int current_score = score_terrain(map, x, y, current, check_radius);

    TerrainType best = current;
    int best_score = current_score;

    // Try several random terrain types
    for (int attempt = 0; attempt < params->attempts_per_tile; attempt++) {
      TerrainType candidate =
          random_weighted_terrain(default_weights, TERRAIN_COUNT);

      int candidate_score = score_terrain(map, x, y, candidate, check_radius);

      // Keep the one with highest score (best affinity)
      if (candidate_score > best_score) {
        best = candidate;
        best_score = candidate_score;
      }
    }

    // Apply the best choice directly to map
    map->cells[map_idx].category = best;
  }

  // Step 3: Set tile and passable based on category
  for (int y = region_y; y < region_y + region_height; y++) {
    for (int x = region_x; x < region_x + region_width; x++) {
      int map_idx = y * MAP_WIDTH_MAX + x;
      TerrainType terrain = (TerrainType)map->cells[map_idx].category;

      map->cells[map_idx].tile = terrain_tiles[terrain];
      map->cells[map_idx].passable = terrain_passable[terrain];
      map->cells[map_idx].visible = true;

      // Set water depth (all subsea terrain is underwater)
      map->water_depth[map_idx] = 255; // Deep water
    }
  }
}

// Full-map CSP generation
void mapgen_csp(Map *map, const CSPGenParams *params) {
  mapgen_csp_region(map, 0, 0, map->width, map->height, params);
}
