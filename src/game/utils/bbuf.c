#include "bbuf.h"
#include "../common.h" // IWYU pragma: keep

// Pack functions - little-endian byte-packed encoding
// size grows even if capacity is exceeded (caller can detect overflow)
// but we never write beyond capacity

void bbuf_pack_u8(ByteBuffer *buf, uint8_t value) {
  if (buf->size < buf->capacity) {
    buf->data[buf->size] = value;
  }
  buf->size++;
}

void bbuf_pack_u16(ByteBuffer *buf, uint16_t value) {
  if (buf->size + 0 < buf->capacity)
    buf->data[buf->size + 0] = (uint8_t)(value & 0xFF);
  if (buf->size + 1 < buf->capacity)
    buf->data[buf->size + 1] = (uint8_t)((value >> 8) & 0xFF);
  buf->size += 2;
}

void bbuf_pack_u32(ByteBuffer *buf, uint32_t value) {
  if (buf->size + 0 < buf->capacity)
    buf->data[buf->size + 0] = (uint8_t)(value & 0xFF);
  if (buf->size + 1 < buf->capacity)
    buf->data[buf->size + 1] = (uint8_t)((value >> 8) & 0xFF);
  if (buf->size + 2 < buf->capacity)
    buf->data[buf->size + 2] = (uint8_t)((value >> 16) & 0xFF);
  if (buf->size + 3 < buf->capacity)
    buf->data[buf->size + 3] = (uint8_t)((value >> 24) & 0xFF);
  buf->size += 4;
}

void bbuf_pack_u64(ByteBuffer *buf, uint64_t value) {
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
}

void bbuf_pack_i8(ByteBuffer *buf, int8_t value) {
  bbuf_pack_u8(buf, (uint8_t)value);
}

void bbuf_pack_i16(ByteBuffer *buf, int16_t value) {
  bbuf_pack_u16(buf, (uint16_t)value);
}

void bbuf_pack_i32(ByteBuffer *buf, int32_t value) {
  bbuf_pack_u32(buf, (uint32_t)value);
}

void bbuf_pack_i64(ByteBuffer *buf, int64_t value) {
  bbuf_pack_u64(buf, (uint64_t)value);
}

void bbuf_pack_bytes(ByteBuffer *buf, const void *data, uint32_t len) {
  uint32_t to_copy = len;
  if (buf->size + len > buf->capacity) {
    to_copy = (buf->capacity > buf->size) ? (buf->capacity - buf->size) : 0;
  }
  if (to_copy > 0) {
    memcpy(buf->data + buf->size, data, to_copy);
  }
  buf->size += len;
}

// Unpack functions - little-endian byte-packed decoding
// offset is passed by pointer and updated as we read
// asserts that reads don't go out of bounds

uint8_t bbuf_unpack_u8(const ByteBuffer *buf, uint32_t *offset) {
  assert(*offset + 1 <= buf->size);
  return buf->data[(*offset)++];
}

uint16_t bbuf_unpack_u16(const ByteBuffer *buf, uint32_t *offset) {
  assert(*offset + 2 <= buf->size);
  uint16_t value = 0;
  value |= (uint16_t)buf->data[(*offset)++];
  value |= (uint16_t)buf->data[(*offset)++] << 8;
  return value;
}

uint32_t bbuf_unpack_u32(const ByteBuffer *buf, uint32_t *offset) {
  assert(*offset + 4 <= buf->size);
  uint32_t value = 0;
  value |= (uint32_t)buf->data[(*offset)++];
  value |= (uint32_t)buf->data[(*offset)++] << 8;
  value |= (uint32_t)buf->data[(*offset)++] << 16;
  value |= (uint32_t)buf->data[(*offset)++] << 24;
  return value;
}

uint64_t bbuf_unpack_u64(const ByteBuffer *buf, uint32_t *offset) {
  assert(*offset + 8 <= buf->size);
  uint64_t value = 0;
  value |= (uint64_t)buf->data[(*offset)++];
  value |= (uint64_t)buf->data[(*offset)++] << 8;
  value |= (uint64_t)buf->data[(*offset)++] << 16;
  value |= (uint64_t)buf->data[(*offset)++] << 24;
  value |= (uint64_t)buf->data[(*offset)++] << 32;
  value |= (uint64_t)buf->data[(*offset)++] << 40;
  value |= (uint64_t)buf->data[(*offset)++] << 48;
  value |= (uint64_t)buf->data[(*offset)++] << 56;
  return value;
}

int8_t bbuf_unpack_i8(const ByteBuffer *buf, uint32_t *offset) {
  return (int8_t)bbuf_unpack_u8(buf, offset);
}

int16_t bbuf_unpack_i16(const ByteBuffer *buf, uint32_t *offset) {
  return (int16_t)bbuf_unpack_u16(buf, offset);
}

int32_t bbuf_unpack_i32(const ByteBuffer *buf, uint32_t *offset) {
  return (int32_t)bbuf_unpack_u32(buf, offset);
}

int64_t bbuf_unpack_i64(const ByteBuffer *buf, uint32_t *offset) {
  return (int64_t)bbuf_unpack_u64(buf, offset);
}

void bbuf_unpack_bytes(const ByteBuffer *buf, uint32_t *offset, void *dest,
                       uint32_t len) {
  assert(*offset + len <= buf->size);
  memcpy(dest, buf->data + *offset, len);
  *offset += len;
}
