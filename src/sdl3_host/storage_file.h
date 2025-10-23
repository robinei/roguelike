#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// Hash map for chunk_key -> StorageHashSlot lookup
// Simple fixed-size hash table with linear probing
#define HASH_TABLE_SIZE 16384

// Result codes for storage operations
typedef enum {
  STORAGE_OK = 0,           // Success
  STORAGE_NOT_FOUND,        // Key not found (expected)
  STORAGE_BUFFER_TOO_SMALL, // Buffer too small (expected)
  STORAGE_INVALID_ARGUMENT, // Invalid argument (unexpected)
  STORAGE_IO_ERROR,         // File I/O failure (unexpected)
  STORAGE_INTERNAL_ERROR,   // Internal error (unexpected)
  STORAGE_CORRUPTION,       // Data corruption detected (unexpected)
  STORAGE_TABLE_FULL,       // Hash table full (unexpected)
} StorageResult;

typedef struct {
  uint64_t key;    // 0 = empty slot
  uint32_t offset; // Offset in file where this entry starts
  uint32_t size;   // Data size
} StorageHashSlot;

typedef struct {
  FILE *file;
  char path[512];
  char error[256];

  StorageHashSlot hash_table[HASH_TABLE_SIZE];
  uint32_t useful_bytes;
  uint32_t wasted_bytes;
} StorageFile;

// Append-only log-structured chunk storage with CRC32 validation
//
// File format:
//   [FileHeader: magic, version]
//   [ChunkEntry 0]
//   [ChunkEntry 1]
//   ...
//
// ChunkEntry format (16 byte header + data):
//   uint32_t crc32       // CRC32 of (size + chunk_key + data)
//   uint32_t size        // Data size in bytes, 0 = tombstone (delete marker)
//   uint64_t chunk_key   // Unique chunk identifier
//   uint8_t data[size]   // Chunk data
//
// Properties:
//   - All operations append entries (single atomic write)
//   - Last entry for a given chunk_key wins
//   - Delete appends tombstone (size=0)
//   - Compaction removes old/deleted entries
//   - CRC32 validation detects corruption
//   - Index rebuilt in memory on open by scanning file

// Open or create storage file at given path
// Scans file and builds in-memory index
// Returns STORAGE_OK on success
StorageResult storage_file_open(StorageFile *storage, const char *path);

// Close storage file
void storage_file_close(StorageFile *storage);

// Get last error message (or empty string if no error)
const char *storage_file_error(StorageFile *storage);

// Get chunk data by key
// Returns STORAGE_OK if found and valid
// Returns STORAGE_NOT_FOUND if key not found
// Returns STORAGE_BUFFER_TOO_SMALL if out_data buffer too small (sets *out_size
// to required size) If out_data is NULL, only returns size in *out_size
StorageResult storage_file_get(StorageFile *storage, uint64_t chunk_key,
                               void *out_data, uint32_t *out_size);

// Set chunk data (appends new entry)
// Last write for a given chunk_key wins
// Returns STORAGE_OK on success
StorageResult storage_file_set(StorageFile *storage, uint64_t chunk_key,
                               const void *data, uint32_t data_size);

// Delete chunk by key (appends tombstone entry)
// Returns STORAGE_OK on success
// Returns STORAGE_NOT_FOUND if key not found
StorageResult storage_file_del(StorageFile *storage, uint64_t chunk_key);

// Compact storage file (removes old/deleted entries)
// Rewrites file with only latest version of each chunk
// Called automatically when fragmentation exceeds threshold
// Returns STORAGE_OK on success
StorageResult storage_file_compact(StorageFile *storage);

#define STORAGE_FILE_TESTS
#ifdef STORAGE_FILE_TESTS
// Run comprehensive test suite
// Tests basic operations, persistence, corruption handling, etc.
void storage_file_run_tests(void);
#endif
