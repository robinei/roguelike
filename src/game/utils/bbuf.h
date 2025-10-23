#pragma once

#include <stdint.h>

typedef struct {
  uint32_t size;     // Write position (for packing)
  uint32_t read_pos; // Read position (for unpacking)
  uint32_t capacity;
  uint8_t *data;
} ByteBuffer;

// Pack/unpack functions with optional debug labels
void bbuf_pack_u8(ByteBuffer *buf, uint8_t value, const char *label);
void bbuf_pack_u16(ByteBuffer *buf, uint16_t value, const char *label);
void bbuf_pack_u32(ByteBuffer *buf, uint32_t value, const char *label);
void bbuf_pack_u64(ByteBuffer *buf, uint64_t value, const char *label);

void bbuf_pack_i8(ByteBuffer *buf, int8_t value, const char *label);
void bbuf_pack_i16(ByteBuffer *buf, int16_t value, const char *label);
void bbuf_pack_i32(ByteBuffer *buf, int32_t value, const char *label);
void bbuf_pack_i64(ByteBuffer *buf, int64_t value, const char *label);

void bbuf_pack_bytes(ByteBuffer *buf, const void *data, uint32_t len,
                     const char *label);

uint8_t bbuf_unpack_u8(ByteBuffer *buf, const char *label);
uint16_t bbuf_unpack_u16(ByteBuffer *buf, const char *label);
uint32_t bbuf_unpack_u32(ByteBuffer *buf, const char *label);
uint64_t bbuf_unpack_u64(ByteBuffer *buf, const char *label);

int8_t bbuf_unpack_i8(ByteBuffer *buf, const char *label);
int16_t bbuf_unpack_i16(ByteBuffer *buf, const char *label);
int32_t bbuf_unpack_i32(ByteBuffer *buf, const char *label);
int64_t bbuf_unpack_i64(ByteBuffer *buf, const char *label);

void bbuf_unpack_bytes(ByteBuffer *buf, void *dest, uint32_t len,
                       const char *label);
