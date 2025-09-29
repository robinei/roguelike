#pragma once

#include <string.h>

#define GET_COMPONENT_BITVEC(comp) (world.comp##_bitvec)

#define AND_BITVECS_1(comp1, word_idx) (GET_COMPONENT_BITVEC(comp1)[word_idx])

#define AND_BITVECS_2(comp1, comp2, word_idx)                                  \
  (GET_COMPONENT_BITVEC(comp1)[word_idx] &                                     \
   GET_COMPONENT_BITVEC(comp2)[word_idx])

#define AND_BITVECS_3(comp1, comp2, comp3, word_idx)                           \
  (GET_COMPONENT_BITVEC(comp1)[word_idx] &                                     \
   GET_COMPONENT_BITVEC(comp2)[word_idx] &                                     \
   GET_COMPONENT_BITVEC(comp3)[word_idx])

#define AND_BITVECS_4(comp1, comp2, comp3, comp4, word_idx)                    \
  (GET_COMPONENT_BITVEC(comp1)[word_idx] &                                     \
   GET_COMPONENT_BITVEC(comp2)[word_idx] &                                     \
   GET_COMPONENT_BITVEC(comp3)[word_idx] &                                     \
   GET_COMPONENT_BITVEC(comp4)[word_idx])

#define AND_BITVECS_5(comp1, comp2, comp3, comp4, comp5, word_idx)             \
  (GET_COMPONENT_BITVEC(comp1)[word_idx] &                                     \
   GET_COMPONENT_BITVEC(comp2)[word_idx] &                                     \
   GET_COMPONENT_BITVEC(comp3)[word_idx] &                                     \
   GET_COMPONENT_BITVEC(comp4)[word_idx] &                                     \
   GET_COMPONENT_BITVEC(comp5)[word_idx])

#define FIRST_COMPONENT(a, ...) a
#define SECOND_COMPONENT(a, b, ...) b
#define THIRD_COMPONENT(a, b, c, ...) c
#define FOURTH_COMPONENT(a, b, c, d, ...) d
#define FIFTH_COMPONENT(a, b, c, d, e, ...) e

#define COUNT_ARGS(...) COUNT_ARGS_(__VA_ARGS__, 5, 4, 3, 2, 1)
#define COUNT_ARGS_(a, b, c, d, e, N, ...) N

#define CONCAT(a, b) a##b
#define SELECT_AND_BITVECS(N) CONCAT(AND_BITVECS_, N)

#define EXPAND_COMPONENTS_2(components)                                        \
  FIRST_COMPONENT components, SECOND_COMPONENT components
#define EXPAND_COMPONENTS_3(components)                                        \
  EXPAND_COMPONENTS_2(components), THIRD_COMPONENT components
#define EXPAND_COMPONENTS_4(components)                                        \
  EXPAND_COMPONENTS_3(components), FOURTH_COMPONENT components
#define EXPAND_COMPONENTS_5(components)                                        \
  EXPAND_COMPONENTS_4(components), FIFTH_COMPONENT components

#define SELECT_EXPAND(N) CONCAT(EXPAND_COMPONENTS_, N)

#define DISPATCH_AND_BITVECS_IMPL(n, components, word_idx)                     \
  DISPATCH_AND_BITVECS_##n(components, word_idx)

#define DISPATCH_AND_BITVECS(n, components, word_idx)                          \
  DISPATCH_AND_BITVECS_IMPL(n, components, word_idx)

#define DISPATCH_AND_BITVECS_1(components, word_idx)                           \
  AND_BITVECS_1(FIRST_COMPONENT components, word_idx)

#define DISPATCH_AND_BITVECS_2(components, word_idx)                           \
  AND_BITVECS_2(FIRST_COMPONENT components, SECOND_COMPONENT components,       \
                word_idx)

#define DISPATCH_AND_BITVECS_3(components, word_idx)                           \
  AND_BITVECS_3(FIRST_COMPONENT components, SECOND_COMPONENT components,       \
                THIRD_COMPONENT components, word_idx)

#define DISPATCH_AND_BITVECS_4(components, word_idx)                           \
  AND_BITVECS_4(FIRST_COMPONENT components, SECOND_COMPONENT components,       \
                THIRD_COMPONENT components, FOURTH_COMPONENT components,       \
                word_idx)

#define DISPATCH_AND_BITVECS_5(components, word_idx)                           \
  AND_BITVECS_5(FIRST_COMPONENT components, SECOND_COMPONENT components,       \
                THIRD_COMPONENT components, FOURTH_COMPONENT components,       \
                FIFTH_COMPONENT components, word_idx)

#define ITER_QUERY(entity_var, components)                                     \
  for (uint32_t entity_var, _word_idx = 0;                                     \
       _word_idx <= world.entity_count / 64; _word_idx++)                      \
    for (uint64_t _word = DISPATCH_AND_BITVECS(COUNT_ARGS components,          \
                                               components, _word_idx);         \
         _word && (entity_var = _word_idx * 64 + __builtin_ctzll(_word), 1);   \
         _word &= _word - 1)

#define SET_COMPONENT_BIT(entity_idx, comp)                                    \
  do {                                                                         \
    uint32_t word_idx = entity_idx / 64;                                       \
    uint32_t bit_idx = entity_idx % 64;                                        \
    world.comp##_bitvec[word_idx] |= (1ULL << bit_idx);                        \
  } while (0)

#define CLEAR_COMPONENT_BIT(entity_idx, comp)                                  \
  do {                                                                         \
    uint32_t word_idx = entity_idx / 64;                                       \
    uint32_t bit_idx = entity_idx % 64;                                        \
    world.comp##_bitvec[word_idx] &= ~(1ULL << bit_idx);                       \
  } while (0)

#define ADD_COMPONENT(handle, comp, value)                                     \
  do {                                                                         \
    uint32_t index = unwrap_handle(handle);                                    \
    world.comp[index] = value;                                                 \
    SET_COMPONENT_BIT(index, comp);                                            \
  } while (0)

#define REMOVE_COMPONENT(handle, comp)                                         \
  do {                                                                         \
    uint32_t index = unwrap_handle(handle);                                    \
    CLEAR_COMPONENT_BIT(index, comp);                                          \
    memset(&world.comp[index], 0, sizeof(world.comp[index]));                  \
  } while (0)