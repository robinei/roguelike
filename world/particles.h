#pragma once

typedef enum {
  PARTICLE_TYPE_BLOOD,
  PARTICLE_TYPE_FOG,
  PARTICLE_TYPE_SNOW,
  PARTICLE_TYPE_RAIN,
  PARTICLE_TYPE_TORCH_SMOKE,
  PARTICLE_TYPE_TORCH_SPARK,
} ParticleType;

typedef struct {
  ParticleType type;
  float x, y;
  float vx, vy;
  float ttl;      // time remaining
  float lifetime; // initial lifetime (for age-based effects in rendering)
} Particle;

// Spawn particle with type-specific default behavior
void particles_spawn(ParticleType type, float x, float y);

// Spawn particle with directional hint (magnitude ignored, only direction
// matters)
void particles_spawn_directed(ParticleType type, float x, float y, float dx,
                              float dy);

void particles_update(float dt);
