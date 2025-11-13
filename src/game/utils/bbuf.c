#include "bbuf.h"
#include "../common.h" // IWYU pragma: keep
#include "print.h"

// Define this to enable debug label validation
#define BBUF_DEBUG_LABELS

#ifdef BBUF_DEBUG_LABELS
static void pack_label(ByteBuffer *buf, const char *label) {
  if (!label)
    label = "";
  uint8_t len = (uint8_t)strlen(label);
  if (len > 255)
    len = 255;

  // Pack length byte
  if (buf->size < buf->capacity) {
    buf->data[buf->size] = len;
  }
  buf->size++;

  // Pack string bytes
  for (uint8_t i = 0; i < len; i++) {
    if (buf->size < buf->capacity) {
      buf->data[buf->size] = (uint8_t)label[i];
    }
    buf->size++;
  }
}

static void unpack_label(ByteBuffer *buf, const char *expected_label) {
  // Read length
  assert(buf->read_pos < buf->size);
  uint8_t len = buf->data[buf->read_pos++];

  // Read label string
  char actual_label[256];
  if (buf->read_pos + len > buf->size) {
    PRINT(msg, 512, "Label read overflow: read_pos=");
    print_int(&msg, buf->read_pos - 1);
    print_str(&msg, ", len=");
    print_int(&msg, len);
    print_str(&msg, ", size=");
    print_int(&msg, buf->size);
    print_str(&msg, ", expected=");
    print_str(&msg, expected_label);
    host_log(LOG_ERROR, msg.data);
  }
  assert(buf->read_pos + len <= buf->size);
  memcpy(actual_label, buf->data + buf->read_pos, len);
  actual_label[len] = '\0';
  buf->read_pos += len;

  // Validate
  if (strcmp(actual_label, expected_label) != 0) {
    PRINT(msg, 512, "ByteBuffer label mismatch! Expected '");
    print_str(&msg, expected_label);
    print_str(&msg, "', got '");
    print_str(&msg, actual_label);
    print_str(&msg, "'");
    host_log(LOG_ERROR, msg.data);
    assert(0 && "ByteBuffer label mismatch");
  }
}
#else
// No-op versions when debug is disabled
static inline void pack_label(ByteBuffer *buf, const char *label) {
  (void)buf;
  (void)label;
}
static inline void unpack_label(ByteBuffer *buf, const char *expected_label) {
  (void)buf;
  (void)expected_label;
}
#endif

// Pack functions - little-endian byte-packed encoding
// size grows even if capacity is exceeded (caller can detect overflow)
// but we never write beyond capacity

void bbuf_pack_u8(ByteBuffer *buf, uint8_t value, const char *label) {
  if (buf->size < buf->capacity) {
    buf->data[buf->size] = value;
  }
  buf->size++;
  pack_label(buf, label);
}

void bbuf_pack_u16(ByteBuffer *buf, uint16_t value, const char *label) {
  if (buf->size + 0 < buf->capacity)
    buf->data[buf->size + 0] = (uint8_t)(value & 0xFF);
  if (buf->size + 1 < buf->capacity)
    buf->data[buf->size + 1] = (uint8_t)((value >> 8) & 0xFF);
  buf->size += 2;
  pack_label(buf, label);
}

void bbuf_pack_u32(ByteBuffer *buf, uint32_t value, const char *label) {
  if (buf->size + 0 < buf->capacity)
    buf->data[buf->size + 0] = (uint8_t)(value & 0xFF);
  if (buf->size + 1 < buf->capacity)
    buf->data[buf->size + 1] = (uint8_t)((value >> 8) & 0xFF);
  if (buf->size + 2 < buf->capacity)
    buf->data[buf->size + 2] = (uint8_t)((value >> 16) & 0xFF);
  if (buf->size + 3 < buf->capacity)
    buf->data[buf->size + 3] = (uint8_t)((value >> 24) & 0xFF);
  buf->size += 4;
  pack_label(buf, label);
}

void bbuf_pack_u64(ByteBuffer *buf, uint64_t value, const char *label) {
  if (buf->size + 0 < buf->capacity)
    buf->data[buf->size + 0] = (uint8_t)(value & 0xFF);
  if (buf->size + 1 < buf->capacity)
    buf->data[buf->size + 1] = (uint8_t)((value >> 8) & 0xFF);
  if (buf->size + 2 < buf->capacity)
    buf->data[buf->size + 2] = (uint8_t)((value >> 16) & 0xFF);
  if (buf->size + 3 < buf->capacity)
    buf->data[buf->size + 3] = (uint8_t)((value >> 24) & 0xFF);
  if (buf->size + 4 < buf->capacity)
    buf->data[buf->size + 4] = (uint8_t)((value >> 32) & 0xFF);
  if (buf->size + 5 < buf->capacity)
    buf->data[buf->size + 5] = (uint8_t)((value >> 40) & 0xFF);
  if (buf->size + 6 < buf->capacity)
    buf->data[buf->size + 6] = (uint8_t)((value >> 48) & 0xFF);
  if (buf->size + 7 < buf->capacity)
    buf->data[buf->size + 7] = (uint8_t)((value >> 56) & 0xFF);
  buf->size += 8;
  pack_label(buf, label);
}

void bbuf_pack_i8(ByteBuffer *buf, int8_t value, const char *label) {
  bbuf_pack_u8(buf, (uint8_t)value, label);
}

void bbuf_pack_i16(ByteBuffer *buf, int16_t value, const char *label) {
  bbuf_pack_u16(buf, (uint16_t)value, label);
}

void bbuf_pack_i32(ByteBuffer *buf, int32_t value, const char *label) {
  bbuf_pack_u32(buf, (uint32_t)value, label);
}

void bbuf_pack_i64(ByteBuffer *buf, int64_t value, const char *label) {
  bbuf_pack_u64(buf, (uint64_t)value, label);
}

void bbuf_pack_bytes(ByteBuffer *buf, const void *data, uint32_t len,
                     const char *label) {
  uint32_t to_copy = len;
  if (buf->size + len > buf->capacity) {
    to_copy = (buf->capacity > buf->size) ? (buf->capacity - buf->size) : 0;
  }
  if (to_copy > 0) {
    memcpy(buf->data + buf->size, data, to_copy);
  }
  buf->size += len;
  pack_label(buf, label);
}

// Unpack functions - little-endian byte-packed decoding
// Uses internal read_pos which is updated as we read
// asserts that reads don't go out of bounds

uint8_t bbuf_unpack_u8(ByteBuffer *buf, const char *label) {
  assert(buf->read_pos + 1 <= buf->size);
  uint8_t value = buf->data[buf->read_pos++];
  unpack_label(buf, label);
  return value;
}

uint16_t bbuf_unpack_u16(ByteBuffer *buf, const char *label) {
  assert(buf->read_pos + 2 <= buf->size);
  uint16_t value = 0;
  value |= (uint16_t)buf->data[buf->read_pos++];
  value |= (uint16_t)buf->data[buf->read_pos++] << 8;
  unpack_label(buf, label);
  return value;
}

uint32_t bbuf_unpack_u32(ByteBuffer *buf, const char *label) {
  assert(buf->read_pos + 4 <= buf->size);
  uint32_t value = 0;
  value |= (uint32_t)buf->data[buf->read_pos++];
  value |= (uint32_t)buf->data[buf->read_pos++] << 8;
  value |= (uint32_t)buf->data[buf->read_pos++] << 16;
  value |= (uint32_t)buf->data[buf->read_pos++] << 24;
  unpack_label(buf, label);
  return value;
}

uint64_t bbuf_unpack_u64(ByteBuffer *buf, const char *label) {
  assert(buf->read_pos + 8 <= buf->size);
  uint64_t value = 0;
  value |= (uint64_t)buf->data[buf->read_pos++];
  value |= (uint64_t)buf->data[buf->read_pos++] << 8;
  value |= (uint64_t)buf->data[buf->read_pos++] << 16;
  value |= (uint64_t)buf->data[buf->read_pos++] << 24;
  value |= (uint64_t)buf->data[buf->read_pos++] << 32;
  value |= (uint64_t)buf->data[buf->read_pos++] << 40;
  value |= (uint64_t)buf->data[buf->read_pos++] << 48;
  value |= (uint64_t)buf->data[buf->read_pos++] << 56;
  unpack_label(buf, label);
  return value;
}

int8_t bbuf_unpack_i8(ByteBuffer *buf, const char *label) {
  return (int8_t)bbuf_unpack_u8(buf, label);
}

int16_t bbuf_unpack_i16(ByteBuffer *buf, const char *label) {
  return (int16_t)bbuf_unpack_u16(buf, label);
}

int32_t bbuf_unpack_i32(ByteBuffer *buf, const char *label) {
  return (int32_t)bbuf_unpack_u32(buf, label);
}

int64_t bbuf_unpack_i64(ByteBuffer *buf, const char *label) {
  return (int64_t)bbuf_unpack_u64(buf, label);
}

void bbuf_unpack_bytes(ByteBuffer *buf, void *dest, uint32_t len,
                       const char *label) {
  assert(buf->read_pos + len <= buf->size);
  memcpy(dest, buf->data + buf->read_pos, len);
  buf->read_pos += len;
  unpack_label(buf, label);
}
