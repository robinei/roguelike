#pragma once

#include <stdint.h>

typedef uint16_t EntityIdentity;

#define FOREACH_ENTITY_IDENTITY(X)                                             \
  X(IDENTITY_NONE, NULL, NULL)                                                 \
  X(IDENTITY_EXCALIBUR, "Excalibur", "King Arthur's legendary sword")          \
  X(IDENTITY_GROG, "Grog", "A burly barbarian")

#define DEFINE_IDENTITY_ENUM(enum_name, name, description) enum_name,
enum { FOREACH_ENTITY_IDENTITY(DEFINE_IDENTITY_ENUM) };
#undef DEFINE_IDENTITY_ENUM
