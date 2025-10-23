#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

// Cross-platform file truncation
// Truncates file to specified size
// Returns true on success, false on failure
bool platform_truncate_file(FILE *f, uint64_t size);

// Cross-platform atomic file replacement
// Atomically replaces dst_path with src_path
// Returns true on success, false on failure
bool platform_atomic_replace(const char *src_path, const char *dst_path);

// Cross-platform file sync (flush to disk)
// Forces OS to write buffered data to physical storage
// Returns true on success, false on failure
bool platform_fsync(FILE *f);

// Get file size by path
// Opens file, gets size via ftell, closes file
// Returns file size on success, -1 on failure
int64_t platform_file_size(const char *path);
