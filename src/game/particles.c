#include "random.h"
#include "world.h"
#include <stdint.h>

#define PI 3.14159265f

static float randf(float min, float max) {
  return min + (max - min) * randomf();
}

static int randi(int min, int max) {
  // Random integer in [min, max] (inclusive)
  return min + (int)(random64() % (uint64_t)(max - min + 1));
}

// Get type-specific TTL (correlated with expected travel distance)
static float get_ttl(ParticleType type) {
  switch (type) {
  case PARTICLE_TYPE_BLOOD:
    return 0.5f; // ~1 tile at ~2 tiles/sec
  case PARTICLE_TYPE_FOG:
    return 10.0f; // drifts slowly over many tiles
  case PARTICLE_TYPE_SNOW:
    return 3.0f; // ~3 tiles at ~1 tile/sec
  case PARTICLE_TYPE_RAIN:
    return 1.0f; // ~6 tiles at ~6 tiles/sec
  case PARTICLE_TYPE_TORCH_SMOKE:
    return 2.0f; // ~3 tiles at ~1.5 tiles/sec
  case PARTICLE_TYPE_TORCH_SPARK:
    return 0.3f; // ~1 tile with parabolic arc
  }
  return 1.0f;
}

// Returns spawn interval in ticks (1 tick = 0.1 seconds)
// Result fits in uint8_t (max 255 ticks = 25.5 seconds)
int particles_gen_spawn_interval(ParticleType type) {
  switch (type) {
  case PARTICLE_TYPE_BLOOD:
    return 0; // Not a continuous generator (spawned on events)
  case PARTICLE_TYPE_FOG:
    return randi(8, 12); // Sparse fog every ~1 second
  case PARTICLE_TYPE_SNOW:
    return randi(1, 3); // Frequent snowflakes
  case PARTICLE_TYPE_RAIN:
    return randi(1, 2); // Very frequent raindrops (0.1-0.2s)
  case PARTICLE_TYPE_TORCH_SMOKE:
    return randi(2, 4); // Smoke puffs every ~0.3 seconds
  case PARTICLE_TYPE_TORCH_SPARK:
    return randi(5, 10); // Occasional sparks every ~0.75 seconds
  }
  return 10; // Default: once per second
}

void particles_spawn_directed(ParticleType type, float x, float y, float dx,
                              float dy) {
  if (WORLD.particle_count >= MAX_PARTICLES) {
    return;
  }

  float vx = 0, vy = 0;
  float ttl = get_ttl(type);

  // Normalize direction vector
  float len = sqrtf(dx * dx + dy * dy);
  if (len > 0.001f) {
    dx /= len;
    dy /= len;
  } else {
    // Zero vector, fallback to random
    dx = 1.0f;
    dy = 0.0f;
  }

  switch (type) {
  case PARTICLE_TYPE_BLOOD: {
    // Blood sprays in cone around direction with random spread
    float angle = atan2f(dy, dx);
    angle += randf(-PI / 6, PI / 6); // ±30 degree cone
    float speed = randf(1.5f, 2.5f);
    vx = cosf(angle) * speed;
    vy = sinf(angle) * speed;
    break;
  }
  case PARTICLE_TYPE_TORCH_SPARK: {
    // Sparks fly in random direction (ignores hint for now)
    float angle = randf(0, 2.0f * PI);
    float speed = randf(2.0f, 4.0f);
    vx = cosf(angle) * speed;
    vy = sinf(angle) * speed;
    break;
  }
  case PARTICLE_TYPE_FOG:
    vx = randf(-0.2f, 0.2f);
    vy = randf(-0.2f, 0.2f);
    break;
  case PARTICLE_TYPE_SNOW:
    vx = randf(-0.3f, 0.3f);
    vy = randf(0.5f, 1.0f);
    break;
  case PARTICLE_TYPE_RAIN:
    vx = randf(-0.1f, 0.1f);
    vy = randf(5.0f, 7.0f);
    break;
  case PARTICLE_TYPE_TORCH_SMOKE:
    vx = randf(-0.3f, 0.3f);
    vy = randf(-2.0f, -1.0f);
    break;
  }

  WORLD.particles[WORLD.particle_count++] = (Particle){.type = type,
                                                       .x = x,
                                                       .y = y,
                                                       .vx = vx,
                                                       .vy = vy,
                                                       .ttl = ttl,
                                                       .lifetime = ttl};
}

void particles_spawn(ParticleType type, float x, float y) {
  particles_spawn_directed(type, x, y, 1.0f, 0.0f);
}

static void particles_unspawn(uint32_t i) {
  if (i == WORLD.particle_count - 1) {
    --WORLD.particle_count;
  } else {
    WORLD.particles[i] = WORLD.particles[--WORLD.particle_count];
  }
}

void particles_update(float dt) {
  WORLD.particle_time += dt;

  for (uint32_t i = 0; i < WORLD.particle_count; ++i) {
    Particle *p = WORLD.particles + i;
    p->ttl -= dt;
    if (p->ttl <= 0) {
      particles_unspawn(i);
      --i; // Recheck this index (now holds swapped particle)
      continue;
    }

    // Update velocity based on particle type (no acceleration for most)
    switch (p->type) {
    case PARTICLE_TYPE_BLOOD:
      // Blood decelerates and falls (parabolic arc)
      p->vx *= 0.92f;
      p->vy += 8.0f * dt; // gravity
      break;
    case PARTICLE_TYPE_TORCH_SPARK:
      // Sparks decelerate and fall (parabolic arc)
      p->vx *= 0.95f;
      p->vy += 15.0f * dt; // strong gravity
      break;
    case PARTICLE_TYPE_FOG:
      // Fog drifts with sinusoidal wobble
      p->vx = 0.3f * sinf(WORLD.particle_time * 1.5f + (float)i * 0.3f);
      break;
    case PARTICLE_TYPE_SNOW:
      // Snow wobbles side to side while falling (sine wave)
      p->vx = 0.5f * sinf(WORLD.particle_time * 2.0f + (float)i * 0.5f);
      break;
    case PARTICLE_TYPE_TORCH_SMOKE:
      // Smoke wobbles as it rises
      p->vx = 0.4f * sinf(WORLD.particle_time * 2.5f + (float)i * 0.2f);
      p->vy *= 0.98f; // slight deceleration
      break;
    case PARTICLE_TYPE_RAIN:
      // Rain maintains constant velocity (already at terminal)
      break;
    }

    // Update position
    p->x += p->vx * dt;
    p->y += p->vy * dt;
  }
}