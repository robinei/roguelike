#pragma once

#include <stdint.h>

typedef struct {
  uint32_t size;
  uint32_t capacity;
  uint8_t *data;
} ByteBuffer;

void bbuf_pack_u8(ByteBuffer *buf, uint8_t value);
void bbuf_pack_u16(ByteBuffer *buf, uint16_t value);
void bbuf_pack_u32(ByteBuffer *buf, uint32_t value);
void bbuf_pack_u64(ByteBuffer *buf, uint64_t value);

void bbuf_pack_i8(ByteBuffer *buf, int8_t value);
void bbuf_pack_i16(ByteBuffer *buf, int16_t value);
void bbuf_pack_i32(ByteBuffer *buf, int32_t value);
void bbuf_pack_i64(ByteBuffer *buf, int64_t value);

void bbuf_pack_bytes(ByteBuffer *buf, const void *data, uint32_t len);

uint8_t bbuf_unpack_u8(const ByteBuffer *buf, uint32_t *offset);
uint16_t bbuf_unpack_u16(const ByteBuffer *buf, uint32_t *offset);
uint32_t bbuf_unpack_u32(const ByteBuffer *buf, uint32_t *offset);
uint64_t bbuf_unpack_u64(const ByteBuffer *buf, uint32_t *offset);

int8_t bbuf_unpack_i8(const ByteBuffer *buf, uint32_t *offset);
int16_t bbuf_unpack_i16(const ByteBuffer *buf, uint32_t *offset);
int32_t bbuf_unpack_i32(const ByteBuffer *buf, uint32_t *offset);
int64_t bbuf_unpack_i64(const ByteBuffer *buf, uint32_t *offset);

void bbuf_unpack_bytes(const ByteBuffer *buf, uint32_t *offset, void *dest,
                       uint32_t len);
