#pragma once

#include <stdint.h>

// PrintBuf - lightweight string building without format strings
typedef struct {
  char *data;
  int32_t capacity;
  int32_t length;
} PrintBuf;

// Declare a PrintBuf with stack-allocated buffer and initial string
#define PRINT(name, size, init_str)                                            \
  char _##name##_buffer[size];                                                 \
  PrintBuf name = {.data = _##name##_buffer, .capacity = size, .length = 0};   \
  print_str(&name, init_str)

// Append operations (all auto-terminate with null byte)
static inline void print_str(PrintBuf *buf, const char *str) {
  while (*str && buf->length < buf->capacity - 1) {
    buf->data[buf->length++] = *str++;
  }
  buf->data[buf->length] = '\0';
}

static inline void print_char(PrintBuf *buf, char c) {
  if (buf->length < buf->capacity - 1) {
    buf->data[buf->length++] = c;
    buf->data[buf->length] = '\0';
  }
}

static inline void print_uint(PrintBuf *buf, uint64_t value) {
  if (value == 0) {
    print_char(buf, '0');
    return;
  }

  // Find the highest power of 10
  uint64_t divisor = 1;
  uint64_t temp = value;
  while (temp >= 10) {
    divisor *= 10;
    temp /= 10;
  }

  // Append digits from most significant to least (no temp buffer needed)
  while (divisor > 0) {
    print_char(buf, '0' + (value / divisor) % 10);
    divisor /= 10;
  }
}

static inline void print_int(PrintBuf *buf, int64_t value) {
  if (value < 0) {
    print_char(buf, '-');
    value = -value;
  }
  print_uint(buf, (uint64_t)value);
}
