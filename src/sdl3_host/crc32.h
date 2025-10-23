#pragma once

#include <stdint.h>

uint32_t crc32_init(void);
uint32_t crc32_update(uint32_t crc, const void *blk_adr, uint32_t blk_len);
uint32_t crc32_finalize(uint32_t crc);