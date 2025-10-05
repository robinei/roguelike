#pragma once

#include <stdint.h>

void splitmix64_seed(uint64_t seed);
uint64_t splitmix64_next_with_state(uint64_t *state);
uint64_t splitmix64_next(void);
float splitmix64_nextf(void);
