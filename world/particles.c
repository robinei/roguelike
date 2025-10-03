#include "particles.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#define MAX_PARTICLES 1024
#define PI 3.14159265f

static uint32_t particle_count = 0;
static Particle particles[MAX_PARTICLES];

// Simple random float between min and max
static float randf(float min, float max) {
  return min + (max - min) * ((float)rand() / (float)RAND_MAX);
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

void particles_spawn_directed(ParticleType type, float x, float y, float dx,
                              float dy) {
  if (particle_count >= MAX_PARTICLES) {
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

  particles[particle_count++] = (Particle){.type = type,
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
  if (i == particle_count - 1) {
    --particle_count;
  } else {
    particles[i] = particles[--particle_count];
  }
}

void particles_update(float dt) {
  static float time = 0;
  time += dt;

  for (uint32_t i = 0; i < particle_count; ++i) {
    Particle *p = particles + i;
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
      p->vx = 0.3f * sinf(time * 1.5f + (float)i * 0.3f);
      break;
    case PARTICLE_TYPE_SNOW:
      // Snow wobbles side to side while falling (sine wave)
      p->vx = 0.5f * sinf(time * 2.0f + (float)i * 0.5f);
      break;
    case PARTICLE_TYPE_TORCH_SMOKE:
      // Smoke wobbles as it rises
      p->vx = 0.4f * sinf(time * 2.5f + (float)i * 0.2f);
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
