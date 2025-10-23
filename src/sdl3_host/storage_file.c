#include "storage_file.h"
#include "crc32.h"
#include "platform.h"
#include <inttypes.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define STORAGE_MAGIC 0x524C434B // "RLCK"
#define STORAGE_VERSION 1
#define FRAGMENTATION_THRESHOLD 0.5f

// File header (8 bytes)
typedef struct {
  uint32_t magic;
  uint32_t version;
} FileHeader;

// Entry header (16 bytes)
typedef struct {
  uint32_t crc32;
  uint32_t size;
  uint64_t chunk_key;
} EntryHeader;

static inline uint64_t splittable64(uint64_t x) {
  x ^= x >> 30;
  x *= 0xbf58476d1ce4e5b9;
  x ^= x >> 27;
  x *= 0x94d049bb133111eb;
  x ^= x >> 31;
  return x;
}

// Hash function
static uint32_t hash_key(uint64_t key) {
  return (uint32_t)(splittable64(key) % HASH_TABLE_SIZE);
}

// Set error message on storage file
static void set_error(StorageFile *storage, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vsnprintf(storage->error, sizeof(storage->error), fmt, args);
  va_end(args);
}

static void clear_error(StorageFile *storage) { storage->error[0] = '\0'; }

// Insert or update entry in hash table
// Returns false if table is full
static bool hash_put(StorageFile *storage, uint64_t key, uint32_t file_offset,
                     uint32_t size) {
  uint32_t idx = hash_key(key);
  uint32_t probes = 0;

  // Linear probing with bounds check
  while (storage->hash_table[idx].key != 0 &&
         storage->hash_table[idx].key != key) {
    if (++probes >= HASH_TABLE_SIZE) {
      return false; // Table full, cannot insert
    }
    idx = (idx + 1) % HASH_TABLE_SIZE;
  }

  if (storage->hash_table[idx].key != 0) {
    uint32_t old_size = storage->hash_table[idx].size;
    storage->useful_bytes -= old_size;
    storage->wasted_bytes += old_size;
  }
  storage->useful_bytes += size;
  storage->hash_table[idx] =
      (StorageHashSlot){.key = key, .offset = file_offset, .size = size};
  return true;
}

// Remove entry from hash table
static void hash_remove(StorageFile *storage, uint64_t key) {
  uint32_t idx = hash_key(key);
  uint32_t probes = 0;

  // Linear probing to find key
  while (storage->hash_table[idx].key != 0) {
    if (storage->hash_table[idx].key == key) {
      uint32_t old_size = storage->hash_table[idx].size;
      storage->useful_bytes -= old_size;
      storage->wasted_bytes += old_size;
      storage->hash_table[idx].key = 0;
      return;
    }
    if (++probes >= HASH_TABLE_SIZE) {
      return; // Not found
    }
    idx = (idx + 1) % HASH_TABLE_SIZE;
  }
}

// Find entry in hash table
static StorageHashSlot *hash_get(StorageFile *storage, uint64_t key) {
  uint32_t idx = hash_key(key);
  uint32_t probes = 0;

  // Linear probing to find key
  while (storage->hash_table[idx].key != 0) {
    if (storage->hash_table[idx].key == key) {
      return &storage->hash_table[idx];
    }
    if (++probes >= HASH_TABLE_SIZE) {
      return NULL; // Not found
    }
    idx = (idx + 1) % HASH_TABLE_SIZE;
  }
  return NULL;
}

static EntryHeader compute_header(uint64_t chunk_key, const void *data,
                                  size_t data_size) {
  EntryHeader hdr;
  hdr.size = (uint32_t)data_size;
  hdr.chunk_key = chunk_key;

  // Compute CRC32 over (size + chunk_key + data)
  uint32_t crc = crc32_init();
  crc = crc32_update(crc, &hdr.size, 4);
  crc = crc32_update(crc, &hdr.chunk_key, 8);
  if (data) {
    crc = crc32_update(crc, data, data_size);
    crc = crc32_finalize(crc);
  } else if (data_size == 0) {
    crc = crc32_finalize(crc);
  }
  hdr.crc32 = crc;
  return hdr;
}

// Scan file and build index
static StorageResult scan_file(StorageFile *storage) {
  // Clear hash table
  memset(storage->hash_table, 0, sizeof(storage->hash_table));
  storage->useful_bytes = 0;
  storage->wasted_bytes = 0;

  // Read and validate file header
  if (fseek(storage->file, 0, SEEK_SET) != 0) {
    set_error(storage, "Failed to seek to file header");
    return STORAGE_IO_ERROR;
  }
  FileHeader file_hdr;
  if (fread(&file_hdr, sizeof(FileHeader), 1, storage->file) != 1) {
    set_error(storage, "Failed to read file header");
    return STORAGE_IO_ERROR;
  }
  if (file_hdr.magic != STORAGE_MAGIC) {
    set_error(storage, "Invalid magic number: expected 0x%08x, got 0x%08x",
              STORAGE_MAGIC, file_hdr.magic);
    return STORAGE_CORRUPTION;
  }
  if (file_hdr.version != STORAGE_VERSION) {
    set_error(storage, "Unsupported version: expected %u, got %u",
              STORAGE_VERSION, file_hdr.version);
    return STORAGE_CORRUPTION;
  }

  int64_t last_valid_offset = sizeof(FileHeader);

  while (!feof(storage->file)) {
    int64_t entry_start = ftell(storage->file);
    if (entry_start < 0) {
      set_error(storage, "ftell failed during scan");
      return STORAGE_IO_ERROR;
    }
    if (entry_start > UINT32_MAX) {
      set_error(storage, "File size exceeds 4GB limit");
      return STORAGE_IO_ERROR;
    }

    // Read entry header
    EntryHeader hdr;
    if (fread(&hdr, sizeof(EntryHeader), 1, storage->file) != 1) {
      // Incomplete header at end of file - truncate at last_valid_offset
      break;
    }

    // Validate CRC32 over (size + chunk_key + data)
    EntryHeader computed = compute_header(hdr.chunk_key, NULL, hdr.size);
    if (hdr.size > 0) {
      // Stream data through stack buffer and update CRC
      uint8_t stream_buffer[65536];
      uint32_t bytes_remaining = hdr.size;
      uint32_t crc =
          computed.crc32; // Already includes size + chunk_key, not finalized
      bool incomplete_read = false;

      while (bytes_remaining > 0) {
        uint32_t chunk_size = bytes_remaining < sizeof(stream_buffer)
                                  ? bytes_remaining
                                  : sizeof(stream_buffer);
        size_t bytes_read = fread(stream_buffer, 1, chunk_size, storage->file);
        if (bytes_read != chunk_size) {
          // Incomplete data at end of file - truncate at last_valid_offset
          incomplete_read = true;
          break;
        }
        crc = crc32_update(crc, stream_buffer, chunk_size);
        bytes_remaining -= chunk_size;
      }

      if (incomplete_read) {
        break;
      }

      computed.crc32 = crc32_finalize(crc);
    }

    if (computed.crc32 != hdr.crc32) {
      // CRC mismatch - check if we're at end of file
      int64_t current_pos = ftell(storage->file);
      if (current_pos < 0) {
        set_error(storage, "ftell failed during corruption check");
        return STORAGE_IO_ERROR;
      }

      if (fseek(storage->file, 0, SEEK_END) != 0) {
        set_error(storage, "fseek failed during corruption check");
        return STORAGE_IO_ERROR;
      }

      int64_t file_end = ftell(storage->file);
      if (file_end < 0) {
        set_error(storage, "ftell failed for file end");
        return STORAGE_IO_ERROR;
      }

      if (current_pos >= file_end) {
        // At end of file - incomplete write, safe to truncate at
        // last_valid_offset
        break;
      } else {
        // Mid-file corruption - don't truncate
        set_error(storage,
                  "Data corruption detected at offset %" PRId64 " (mid-file)",
                  entry_start);
        return STORAGE_CORRUPTION;
      }
    }

    // Valid entry - update index
    if (hdr.size > 0) {
      if (!hash_put(storage, hdr.chunk_key, (uint32_t)entry_start, hdr.size)) {
        set_error(storage, "Hash table full during scan (max %d chunks)",
                  HASH_TABLE_SIZE);
        return STORAGE_TABLE_FULL;
      }
    } else {
      // Tombstone - remove from index
      hash_remove(storage, hdr.chunk_key);
    }

    // Update last valid offset to after this entry
    last_valid_offset = ftell(storage->file);
    if (last_valid_offset < 0) {
      set_error(storage, "ftell failed after valid entry");
      return STORAGE_IO_ERROR;
    }
  }

  // Truncate file to remove any corrupted/incomplete data
  int64_t current_size = ftell(storage->file);
  if (current_size < 0) {
    set_error(storage, "ftell failed at end of scan");
    return STORAGE_IO_ERROR;
  }

  if (last_valid_offset < current_size) {
    if (!platform_truncate_file(storage->file, last_valid_offset)) {
      set_error(storage, "Failed to truncate corrupted data");
      return STORAGE_IO_ERROR;
    }
    fseek(storage->file, 0, SEEK_END); // Best effort, ignore seek error
  }

  clear_error(storage);
  return STORAGE_OK;
}

StorageResult storage_file_open(StorageFile *storage, const char *path) {
  // Try to open existing file
  FILE *f = fopen(path, "r+b");
  if (!f) {
    // Doesn't exist - create new file with header (w+b = read/write, create)
    f = fopen(path, "w+b");
    if (!f) {
      set_error(storage, "Failed to create file: %s", path);
      return STORAGE_IO_ERROR;
    }

    FileHeader hdr = {.magic = STORAGE_MAGIC, .version = STORAGE_VERSION};
    if (fwrite(&hdr, sizeof(FileHeader), 1, f) != 1) {
      set_error(storage, "Failed to write file header to: %s", path);
      fclose(f);
      return STORAGE_IO_ERROR;
    }
    if (!platform_fsync(f)) {
      set_error(storage, "Failed to sync file header to: %s", path);
      fclose(f);
      return STORAGE_IO_ERROR;
    }
  }

  storage->file = f;
  snprintf(storage->path, sizeof(storage->path), "%s", path);

  // Scan file, validate header, and build index
  return scan_file(storage);
}

void storage_file_close(StorageFile *storage) {
  if (storage->file) {
    fclose(storage->file);
  }
  memset(storage, 0, sizeof(*storage));
}

const char *storage_file_error(StorageFile *storage) {
  return storage ? storage->error : "";
}

StorageResult storage_file_get(StorageFile *storage, uint64_t chunk_key,
                               void *out_data, uint32_t *out_size) {
  if (!chunk_key) {
    set_error(storage, "Invalid chunk key: 0 is reserved");
    return STORAGE_INVALID_ARGUMENT;
  }
  if (!out_size) {
    set_error(storage, "out_size parameter cannot be NULL");
    return STORAGE_INVALID_ARGUMENT;
  }

  StorageHashSlot *slot = hash_get(storage, chunk_key);
  if (!slot) {
    clear_error(storage);
    return STORAGE_NOT_FOUND;
  }

  // If caller just wants size, return it
  if (!out_data) {
    *out_size = slot->size;
    clear_error(storage);
    return STORAGE_OK;
  }

  // Check if buffer is large enough
  if (*out_size < slot->size) {
    *out_size = slot->size;
    clear_error(storage);
    return STORAGE_BUFFER_TOO_SMALL;
  }

  // Seek to entry start and read header
  if (fseek(storage->file, slot->offset, SEEK_SET) != 0) {
    set_error(storage, "Failed to seek to offset %u", slot->offset);
    return STORAGE_IO_ERROR;
  }
  EntryHeader hdr;
  if (fread(&hdr, sizeof(EntryHeader), 1, storage->file) != 1) {
    set_error(storage, "Failed to read entry header at offset %u",
              slot->offset);
    return STORAGE_IO_ERROR;
  }

  // Validate header size matches index
  if (hdr.size != slot->size || hdr.chunk_key != chunk_key) {
    set_error(
        storage,
        "Header mismatch: expected size=%u key=%llu, got size=%u key=%llu",
        slot->size, (unsigned long long)chunk_key, hdr.size,
        (unsigned long long)hdr.chunk_key);
    return STORAGE_CORRUPTION;
  }

  // Read data
  if (fread(out_data, 1, slot->size, storage->file) != slot->size) {
    set_error(storage, "Failed to read %u bytes of data at offset %u",
              slot->size, slot->offset);
    return STORAGE_IO_ERROR;
  }

  // Validate CRC32
  EntryHeader computed = compute_header(chunk_key, out_data, slot->size);
  if (computed.crc32 != hdr.crc32) {
    set_error(storage, "CRC32 mismatch: expected 0x%08x, got 0x%08x",
              computed.crc32, hdr.crc32);
    return STORAGE_CORRUPTION;
  }

  *out_size = slot->size;
  clear_error(storage);
  return STORAGE_OK;
}

StorageResult storage_file_set(StorageFile *storage, uint64_t chunk_key,
                               const void *data, uint32_t data_size) {
  if (!chunk_key) {
    set_error(storage, "Invalid chunk key: 0 is reserved");
    return STORAGE_INVALID_ARGUMENT;
  }
  if (!data) {
    set_error(storage, "data parameter cannot be NULL");
    return STORAGE_INVALID_ARGUMENT;
  }
  if (data_size == 0) {
    set_error(storage, "data_size parameter cannot be 0");
    return STORAGE_INVALID_ARGUMENT;
  }

  // Build entry header
  EntryHeader hdr = compute_header(chunk_key, data, data_size);

  // Append entry to end of file
  if (fseek(storage->file, 0, SEEK_END) != 0) {
    set_error(storage, "Failed to seek to end of file");
    return STORAGE_IO_ERROR;
  }

  int64_t entry_offset = ftell(storage->file);
  if (entry_offset < 0) {
    set_error(storage, "ftell failed");
    return STORAGE_IO_ERROR;
  }
  if (entry_offset > UINT32_MAX) {
    set_error(storage, "File size exceeds 4GB limit");
    return STORAGE_IO_ERROR;
  }

  // Write header + data
  if (fwrite(&hdr, sizeof(EntryHeader), 1, storage->file) != 1) {
    set_error(storage, "Failed to write entry header");
    return STORAGE_IO_ERROR;
  }
  if (fwrite(data, 1, data_size, storage->file) != data_size) {
    set_error(storage, "Failed to write %u bytes of data", data_size);
    platform_truncate_file(storage->file, (uint64_t)entry_offset);
    return STORAGE_IO_ERROR;
  }

  // Sync to disk for durability
  if (!platform_fsync(storage->file)) {
    set_error(storage, "Failed to sync data to disk");
    platform_truncate_file(storage->file, (uint64_t)entry_offset);
    return STORAGE_IO_ERROR;
  }

  // Update index only after successful write
  if (!hash_put(storage, chunk_key, (uint32_t)entry_offset, hdr.size)) {
    set_error(storage, "Hash table full (max %d chunks)", HASH_TABLE_SIZE);
    platform_truncate_file(storage->file, (uint64_t)entry_offset);
    return STORAGE_TABLE_FULL;
  }

  // Check if compaction needed
  uint32_t total_data = storage->wasted_bytes + storage->useful_bytes;
  if (total_data > 0 &&
      (float)storage->wasted_bytes / total_data > FRAGMENTATION_THRESHOLD) {
    // Compaction failure is not critical - data is still written
    // Just log and continue
    storage_file_compact(storage);
  }

  clear_error(storage);
  return STORAGE_OK;
}

StorageResult storage_file_del(StorageFile *storage, uint64_t chunk_key) {
  if (!chunk_key) {
    set_error(storage, "Invalid chunk key: 0 is reserved");
    return STORAGE_INVALID_ARGUMENT;
  }
  if (!hash_get(storage, chunk_key)) {
    clear_error(storage);
    return STORAGE_NOT_FOUND;
  }

  // Append tombstone entry (size = 0)
  EntryHeader hdr = compute_header(chunk_key, NULL, 0);

  if (fseek(storage->file, 0, SEEK_END) != 0) {
    set_error(storage, "Failed to seek to end of file");
    return STORAGE_IO_ERROR;
  }

  if (fwrite(&hdr, sizeof(EntryHeader), 1, storage->file) != 1) {
    set_error(storage, "Failed to write tombstone entry");
    return STORAGE_IO_ERROR;
  }

  if (!platform_fsync(storage->file)) {
    set_error(storage, "Failed to sync tombstone to disk");
    return STORAGE_IO_ERROR;
  }

  // Update index
  hash_remove(storage, chunk_key);
  clear_error(storage);
  return STORAGE_OK;
}

StorageResult storage_file_compact(StorageFile *storage) {
  char tmp_path[512];
  snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", storage->path);

  FILE *tmp = fopen(tmp_path, "wb");
  if (!tmp) {
    set_error(storage, "Failed to create temporary file: %s", tmp_path);
    return STORAGE_IO_ERROR;
  }

  // Write file header
  FileHeader file_hdr = {.magic = STORAGE_MAGIC, .version = STORAGE_VERSION};
  if (fwrite(&file_hdr, sizeof(FileHeader), 1, tmp) != 1) {
    set_error(storage, "Failed to write header to temporary file");
    fclose(tmp);
    remove(tmp_path);
    return STORAGE_IO_ERROR;
  }

  // Track expected file size for validation
  int64_t expected_size = sizeof(FileHeader);

  // Copy all active chunks
  for (uint32_t i = 0; i < HASH_TABLE_SIZE; i++) {
    StorageHashSlot *slot = &storage->hash_table[i];
    if (slot->key == 0) {
      continue;
    }
    if (slot->size == 0) {
      set_error(storage, "Internal error: hash slot with key=%llu has size=0",
                (unsigned long long)slot->key);
      fclose(tmp);
      remove(tmp_path);
      return STORAGE_INTERNAL_ERROR;
    }

    // Seek to data start in old file
    if (fseek(storage->file, slot->offset + sizeof(EntryHeader), SEEK_SET) !=
        0) {
      continue; // Skip if can't seek, but continue compacting
    }

    // Reserve space for header in tmp file (write later after computing CRC)
    int64_t entry_offset = ftell(tmp);
    if (entry_offset < 0) {
      set_error(storage, "ftell failed during compaction");
      fclose(tmp);
      remove(tmp_path);
      return STORAGE_IO_ERROR;
    }
    EntryHeader entry_hdr = compute_header(slot->key, NULL, slot->size);
    if (fwrite(&entry_hdr, sizeof(EntryHeader), 1, tmp) != 1) {
      set_error(storage, "Failed to write entry header during compaction");
      fclose(tmp);
      remove(tmp_path);
      return STORAGE_IO_ERROR;
    }

    // Stream data from old file to new file, updating CRC
    uint8_t stream_buffer[65536];
    uint32_t bytes_remaining = slot->size;
    uint32_t crc =
        entry_hdr.crc32; // Already includes size + key, not finalized
    while (bytes_remaining > 0) {
      uint32_t chunk_size = bytes_remaining < sizeof(stream_buffer)
                                ? bytes_remaining
                                : sizeof(stream_buffer);
      if (fread(stream_buffer, 1, chunk_size, storage->file) != chunk_size) {
        set_error(storage, "Failed to read %u bytes during compaction",
                  chunk_size);
        fclose(tmp);
        remove(tmp_path);
        return STORAGE_IO_ERROR;
      }
      if (fwrite(stream_buffer, 1, chunk_size, tmp) != chunk_size) {
        set_error(storage, "Failed to write %u bytes during compaction",
                  chunk_size);
        fclose(tmp);
        remove(tmp_path);
        return STORAGE_IO_ERROR;
      }
      crc = crc32_update(crc, stream_buffer, chunk_size);
      bytes_remaining -= chunk_size;
    }
    entry_hdr.crc32 = crc32_finalize(crc);

    // Go back and write the correct header with finalized CRC
    if (fseek(tmp, entry_offset, SEEK_SET) != 0) {
      set_error(storage, "fseek failed during compaction");
      fclose(tmp);
      remove(tmp_path);
      return STORAGE_IO_ERROR;
    }
    if (fwrite(&entry_hdr, sizeof(EntryHeader), 1, tmp) != 1) {
      set_error(storage, "Failed to write entry header during compaction");
      fclose(tmp);
      remove(tmp_path);
      return STORAGE_IO_ERROR;
    }
    // Return to end of file for next entry
    if (fseek(tmp, 0, SEEK_END) != 0) {
      set_error(storage, "fseek failed during compaction");
      fclose(tmp);
      remove(tmp_path);
      return STORAGE_IO_ERROR;
    }

    // Update expected size
    expected_size += sizeof(EntryHeader) + slot->size;
  }

  // Close temp file before validation
  fclose(tmp);

  // Validate compacted file size matches expected
  int64_t actual_size = platform_file_size(tmp_path);
  if (actual_size < 0) {
    set_error(storage, "Failed to get size of compacted file");
    remove(tmp_path);
    return STORAGE_IO_ERROR;
  }
  if (actual_size != expected_size) {
    set_error(storage,
              "Compaction size mismatch: expected %" PRId64
              " bytes, got %" PRId64 " bytes",
              expected_size, actual_size);
    remove(tmp_path);
    return STORAGE_INTERNAL_ERROR;
  }

  // Close original file before atomic replace
  fclose(storage->file);
  storage->file = NULL;

  // Atomically replace old file with new
  if (!platform_atomic_replace(tmp_path, storage->path)) {
    set_error(storage, "Failed to replace storage file");
    storage->file = fopen(storage->path, "r+b"); // Try to recover
    remove(tmp_path);
    return STORAGE_IO_ERROR;
  }

  // Reopen and rescan
  storage->file = fopen(storage->path, "r+b");
  if (!storage->file) {
    set_error(storage, "Failed to reopen storage file after compaction");
    return STORAGE_IO_ERROR;
  }

  // Rescan file and return result directly
  return scan_file(storage);
}

#ifdef STORAGE_FILE_TESTS

#include <assert.h>
#include <string.h>

// Test result tracking
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static bool test_##name(void)
#define RUN_TEST(name)                                                         \
  do {                                                                         \
    tests_run++;                                                               \
    printf("Running test: %s\n", #name);                                       \
    if (test_##name()) {                                                       \
      tests_passed++;                                                          \
      printf("  PASSED\n");                                                    \
    } else {                                                                   \
      tests_failed++;                                                          \
      printf("  FAILED\n");                                                    \
    }                                                                          \
  } while (0)

#define ASSERT(cond)                                                           \
  do {                                                                         \
    if (!(cond)) {                                                             \
      printf("  FAILED: %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
      return false;                                                            \
    }                                                                          \
  } while (0)

#define ASSERT_EQ(actual, expected)                                            \
  do {                                                                         \
    if ((actual) != (expected)) {                                              \
      printf("  FAILED: %s:%d: %s == %s\n", __FILE__, __LINE__, #actual,       \
             #expected);                                                       \
      printf("    actual:   %d\n", (int)(actual));                             \
      printf("    expected: %d\n", (int)(expected));                           \
      return false;                                                            \
    }                                                                          \
  } while (0)

// Basic operations: create, open, set, get, del
TEST(basic_operations) {
  StorageFile storage = {0};
  StorageResult result;

  // Open (creates new file)
  result = storage_file_open(&storage, "test_basic.dat");
  ASSERT_EQ(result, STORAGE_OK);

  // Write a chunk
  uint8_t write_data[100];
  memset(write_data, 0xAB, sizeof(write_data));
  result = storage_file_set(&storage, 123, write_data, sizeof(write_data));
  ASSERT_EQ(result, STORAGE_OK);

  // Read it back
  uint8_t read_data[100];
  uint32_t size = sizeof(read_data);
  result = storage_file_get(&storage, 123, read_data, &size);
  ASSERT_EQ(result, STORAGE_OK);
  ASSERT_EQ(size, sizeof(write_data));
  ASSERT_EQ(memcmp(write_data, read_data, size), 0);

  // Query size without reading
  uint32_t query_size = 0;
  result = storage_file_get(&storage, 123, NULL, &query_size);
  ASSERT_EQ(result, STORAGE_OK);
  ASSERT_EQ(query_size, sizeof(write_data));

  // Delete chunk
  result = storage_file_del(&storage, 123);
  ASSERT_EQ(result, STORAGE_OK);

  // Verify it's gone
  result = storage_file_get(&storage, 123, read_data, &size);
  ASSERT_EQ(result, STORAGE_NOT_FOUND);

  storage_file_close(&storage);
  remove("test_basic.dat");
  return true;
}

// Test argument validation
TEST(argument_validation) {
  StorageFile storage = {0};
  StorageResult result;

  result = storage_file_open(&storage, "test_validation.dat");
  ASSERT_EQ(result, STORAGE_OK);

  uint8_t data[10] = {0};
  uint32_t size = 10;

  // Invalid key (0 is reserved)
  result = storage_file_set(&storage, 0, data, 10);
  ASSERT_EQ(result, STORAGE_INVALID_ARGUMENT);

  result = storage_file_get(&storage, 0, data, &size);
  ASSERT_EQ(result, STORAGE_INVALID_ARGUMENT);

  result = storage_file_del(&storage, 0);
  ASSERT_EQ(result, STORAGE_INVALID_ARGUMENT);

  // NULL data pointer
  result = storage_file_set(&storage, 1, NULL, 10);
  ASSERT_EQ(result, STORAGE_INVALID_ARGUMENT);

  // Zero size
  result = storage_file_set(&storage, 1, data, 0);
  ASSERT_EQ(result, STORAGE_INVALID_ARGUMENT);

  // NULL out_size pointer
  result = storage_file_get(&storage, 1, data, NULL);
  ASSERT_EQ(result, STORAGE_INVALID_ARGUMENT);

  storage_file_close(&storage);
  remove("test_validation.dat");
  return true;
}

// Test overwrite behavior (last write wins)
TEST(overwrite) {
  StorageFile storage = {0};
  StorageResult result;

  result = storage_file_open(&storage, "test_overwrite.dat");
  ASSERT_EQ(result, STORAGE_OK);

  // Write chunk with value 1
  uint8_t data1[50];
  memset(data1, 1, sizeof(data1));
  result = storage_file_set(&storage, 999, data1, sizeof(data1));
  ASSERT_EQ(result, STORAGE_OK);

  // Overwrite with value 2
  uint8_t data2[50];
  memset(data2, 2, sizeof(data2));
  result = storage_file_set(&storage, 999, data2, sizeof(data2));
  ASSERT_EQ(result, STORAGE_OK);

  // Read back - should get value 2
  uint8_t read_data[50];
  uint32_t size = sizeof(read_data);
  result = storage_file_get(&storage, 999, read_data, &size);
  ASSERT_EQ(result, STORAGE_OK);
  ASSERT_EQ(memcmp(read_data, data2, size), 0);

  storage_file_close(&storage);
  remove("test_overwrite.dat");
  return true;
}

// Test persistence (close and reopen)
TEST(persistence) {
  StorageFile storage = {0};
  StorageResult result;

  // Create and write
  result = storage_file_open(&storage, "test_persist.dat");
  ASSERT_EQ(result, STORAGE_OK);

  uint8_t write_data[200];
  for (int i = 0; i < 200; i++) {
    write_data[i] = (uint8_t)i;
  }
  result = storage_file_set(&storage, 777, write_data, sizeof(write_data));
  ASSERT_EQ(result, STORAGE_OK);

  storage_file_close(&storage);

  // Reopen and verify
  memset(&storage, 0, sizeof(storage));
  result = storage_file_open(&storage, "test_persist.dat");
  ASSERT_EQ(result, STORAGE_OK);

  uint8_t read_data[200];
  uint32_t size = sizeof(read_data);
  result = storage_file_get(&storage, 777, read_data, &size);
  ASSERT_EQ(result, STORAGE_OK);
  ASSERT_EQ(size, sizeof(write_data));
  ASSERT_EQ(memcmp(write_data, read_data, size), 0);

  storage_file_close(&storage);
  remove("test_persist.dat");
  return true;
}

// Test multiple chunks
TEST(multiple_chunks) {
  StorageFile storage = {0};
  StorageResult result;

  result = storage_file_open(&storage, "test_multi.dat");
  ASSERT_EQ(result, STORAGE_OK);

  // Write 100 chunks
  for (uint64_t i = 1; i <= 100; i++) {
    uint8_t data[10];
    memset(data, (uint8_t)i, sizeof(data));
    result = storage_file_set(&storage, i, data, sizeof(data));
    ASSERT_EQ(result, STORAGE_OK);
  }

  // Read them all back
  for (uint64_t i = 1; i <= 100; i++) {
    uint8_t data[10];
    uint32_t size = sizeof(data);
    result = storage_file_get(&storage, i, data, &size);
    ASSERT_EQ(result, STORAGE_OK);
    ASSERT_EQ(size, 10);
    for (int j = 0; j < 10; j++) {
      ASSERT_EQ(data[j], (uint8_t)i);
    }
  }

  storage_file_close(&storage);
  remove("test_multi.dat");
  return true;
}

// Test compaction
TEST(compaction) {
  StorageFile storage = {0};
  StorageResult result;

  result = storage_file_open(&storage, "test_compact.dat");
  ASSERT_EQ(result, STORAGE_OK);

  // Write and overwrite to create fragmentation
  uint8_t data[1000];
  for (int round = 0; round < 5; round++) {
    for (uint64_t i = 1; i <= 10; i++) {
      memset(data, (uint8_t)(round * 10 + i), sizeof(data));
      result = storage_file_set(&storage, i, data, sizeof(data));
      ASSERT_EQ(result, STORAGE_OK);
    }
  }

  // Delete half of them
  for (uint64_t i = 1; i <= 5; i++) {
    result = storage_file_del(&storage, i);
    ASSERT_EQ(result, STORAGE_OK);
  }

  // Get file size before compaction
  storage_file_close(&storage);
  int64_t size_before = platform_file_size("test_compact.dat");
  ASSERT(size_before > 0);

  // Reopen and compact
  result = storage_file_open(&storage, "test_compact.dat");
  ASSERT_EQ(result, STORAGE_OK);

  result = storage_file_compact(&storage);
  ASSERT_EQ(result, STORAGE_OK);

  // Get file size after compaction
  storage_file_close(&storage);
  int64_t size_after = platform_file_size("test_compact.dat");
  ASSERT(size_after > 0);

  // File should be smaller after compaction
  // We wrote 10 chunks 5 times (50 entries) + deleted 5 (5 tombstones)
  // After compaction: only 5 live chunks remain
  // Expected reduction: ~10x (50+5 entries -> 5 entries)
  printf("  Compaction: %lld bytes -> %lld bytes (%.1f%% reduction)\n",
         (long long)size_before, (long long)size_after,
         100.0 * (size_before - size_after) / size_before);
  ASSERT(size_after < size_before);

  // Reopen to verify data integrity after compaction
  result = storage_file_open(&storage, "test_compact.dat");
  ASSERT_EQ(result, STORAGE_OK);

  // Verify remaining chunks still exist
  for (uint64_t i = 6; i <= 10; i++) {
    uint32_t size = sizeof(data);
    result = storage_file_get(&storage, i, data, &size);
    ASSERT_EQ(result, STORAGE_OK);
    ASSERT_EQ(size, 1000);
  }

  // Verify deleted chunks are gone
  for (uint64_t i = 1; i <= 5; i++) {
    uint32_t size = sizeof(data);
    result = storage_file_get(&storage, i, data, &size);
    ASSERT_EQ(result, STORAGE_NOT_FOUND);
  }

  storage_file_close(&storage);
  remove("test_compact.dat");
  return true;
}

// Test buffer too small
TEST(buffer_too_small) {
  StorageFile storage = {0};
  StorageResult result;

  result = storage_file_open(&storage, "test_buffer.dat");
  ASSERT_EQ(result, STORAGE_OK);

  // Write 100 bytes
  uint8_t write_data[100];
  memset(write_data, 0xFF, sizeof(write_data));
  result = storage_file_set(&storage, 555, write_data, sizeof(write_data));
  ASSERT_EQ(result, STORAGE_OK);

  // Try to read with too small buffer
  uint8_t read_data[50];
  uint32_t size = sizeof(read_data);
  result = storage_file_get(&storage, 555, read_data, &size);
  ASSERT_EQ(result, STORAGE_BUFFER_TOO_SMALL);
  ASSERT_EQ(size, 100); // Should return required size

  // Read with correct size
  uint8_t full_data[100];
  size = sizeof(full_data);
  result = storage_file_get(&storage, 555, full_data, &size);
  ASSERT_EQ(result, STORAGE_OK);

  storage_file_close(&storage);
  remove("test_buffer.dat");
  return true;
}

// Test large chunk (stress test streaming)
TEST(large_chunk) {
  StorageFile storage = {0};
  StorageResult result;

  result = storage_file_open(&storage, "test_large.dat");
  ASSERT_EQ(result, STORAGE_OK);

  // Write 1MB chunk (larger than 64KB stream buffer)
  uint32_t chunk_size = 1024 * 1024;
  uint8_t *large_data = malloc(chunk_size);
  ASSERT(large_data != NULL);

  // Fill with pattern
  for (uint32_t i = 0; i < chunk_size; i++) {
    large_data[i] = (uint8_t)(i % 256);
  }

  result = storage_file_set(&storage, 88888, large_data, chunk_size);
  ASSERT_EQ(result, STORAGE_OK);

  // Read back
  uint8_t *read_data = malloc(chunk_size);
  ASSERT(read_data != NULL);

  uint32_t size = chunk_size;
  result = storage_file_get(&storage, 88888, read_data, &size);
  ASSERT_EQ(result, STORAGE_OK);
  ASSERT_EQ(size, chunk_size);
  ASSERT_EQ(memcmp(large_data, read_data, chunk_size), 0);

  free(large_data);
  free(read_data);

  storage_file_close(&storage);
  remove("test_large.dat");
  return true;
}

// Test delete non-existent chunk
TEST(delete_missing) {
  StorageFile storage = {0};
  StorageResult result;

  result = storage_file_open(&storage, "test_del_missing.dat");
  ASSERT_EQ(result, STORAGE_OK);

  // Try to delete chunk that doesn't exist
  result = storage_file_del(&storage, 99999);
  ASSERT_EQ(result, STORAGE_NOT_FOUND);

  storage_file_close(&storage);
  remove("test_del_missing.dat");
  return true;
}

// Test reopen after close
TEST(reopen_after_close) {
  StorageFile storage = {0};
  StorageResult result;

  // Open, write, close
  result = storage_file_open(&storage, "test_reopen.dat");
  ASSERT_EQ(result, STORAGE_OK);

  uint8_t data[20] = {1, 2, 3, 4, 5};
  result = storage_file_set(&storage, 111, data, 5);
  ASSERT_EQ(result, STORAGE_OK);

  storage_file_close(&storage);

  // Reopen same struct
  result = storage_file_open(&storage, "test_reopen.dat");
  ASSERT_EQ(result, STORAGE_OK);

  uint32_t size = sizeof(data);
  result = storage_file_get(&storage, 111, data, &size);
  ASSERT_EQ(result, STORAGE_OK);
  ASSERT_EQ(size, 5);

  storage_file_close(&storage);
  remove("test_reopen.dat");
  return true;
}

// Test recovery from truncated file (incomplete write at tail)
TEST(corruption_truncated_tail) {
  StorageFile storage = {0};
  StorageResult result;

  // Create file with data
  result = storage_file_open(&storage, "test_corrupt_tail.dat");
  ASSERT_EQ(result, STORAGE_OK);

  uint8_t data[100];
  memset(data, 0xCC, sizeof(data));
  result = storage_file_set(&storage, 111, data, sizeof(data));
  ASSERT_EQ(result, STORAGE_OK);

  storage_file_close(&storage);

  // Corrupt: truncate file to remove last 20 bytes
  int64_t file_size = platform_file_size("test_corrupt_tail.dat");
  ASSERT(file_size > 20);

  FILE *f = fopen("test_corrupt_tail.dat", "r+b");
  ASSERT(f != NULL);
  ASSERT(platform_truncate_file(f, file_size - 20));
  fclose(f);

  // Reopen - should auto-recover by truncating to last valid entry
  memset(&storage, 0, sizeof(storage));
  result = storage_file_open(&storage, "test_corrupt_tail.dat");
  ASSERT_EQ(result, STORAGE_OK);

  // Data should be missing (incomplete entry removed)
  uint32_t size = sizeof(data);
  result = storage_file_get(&storage, 111, data, &size);
  ASSERT_EQ(result, STORAGE_NOT_FOUND);

  storage_file_close(&storage);
  remove("test_corrupt_tail.dat");
  return true;
}

// Test detection of mid-file corruption
TEST(corruption_mid_file) {
  StorageFile storage = {0};
  StorageResult result;

  // Create file with two chunks
  result = storage_file_open(&storage, "test_corrupt_mid.dat");
  ASSERT_EQ(result, STORAGE_OK);

  uint8_t data1[100];
  memset(data1, 0xAA, sizeof(data1));
  result = storage_file_set(&storage, 111, data1, sizeof(data1));
  ASSERT_EQ(result, STORAGE_OK);

  uint8_t data2[100];
  memset(data2, 0xBB, sizeof(data2));
  result = storage_file_set(&storage, 222, data2, sizeof(data2));
  ASSERT_EQ(result, STORAGE_OK);

  storage_file_close(&storage);

  // Corrupt: flip a byte in the middle of first chunk's data
  FILE *f = fopen("test_corrupt_mid.dat", "r+b");
  ASSERT(f != NULL);

  // Seek to middle of first entry (header is 8+16=24 bytes)
  ASSERT_EQ(fseek(f, 8 + 16 + 50, SEEK_SET), 0);
  uint8_t corrupt_byte = 0x00;
  ASSERT_EQ(fwrite(&corrupt_byte, 1, 1, f), 1);
  fclose(f);

  // Reopen - should detect corruption and fail
  memset(&storage, 0, sizeof(storage));
  result = storage_file_open(&storage, "test_corrupt_mid.dat");
  ASSERT_EQ(result, STORAGE_CORRUPTION);

  remove("test_corrupt_mid.dat");
  return true;
}

// Test recovery from CRC corruption at tail
TEST(corruption_crc_at_tail) {
  StorageFile storage = {0};
  StorageResult result;

  // Create file with valid chunk
  result = storage_file_open(&storage, "test_corrupt_tail_crc.dat");
  ASSERT_EQ(result, STORAGE_OK);

  uint8_t data1[50];
  memset(data1, 0xAA, sizeof(data1));
  result = storage_file_set(&storage, 111, data1, sizeof(data1));
  ASSERT_EQ(result, STORAGE_OK);

  storage_file_close(&storage);

  // Append incomplete/corrupted chunk at tail
  FILE *f = fopen("test_corrupt_tail_crc.dat", "r+b");
  ASSERT(f != NULL);

  // Add a chunk with correct size but corrupted data
  ASSERT_EQ(fseek(f, 0, SEEK_END), 0);
  EntryHeader bad_hdr;
  bad_hdr.chunk_key = 222;
  bad_hdr.size = 50;
  bad_hdr.crc32 = 0xDEADBEEF; // Wrong CRC

  ASSERT_EQ(fwrite(&bad_hdr, sizeof(bad_hdr), 1, f), 1);

  uint8_t corrupt_data[50];
  memset(corrupt_data, 0xBB, sizeof(corrupt_data));
  ASSERT_EQ(fwrite(corrupt_data, 1, sizeof(corrupt_data), f),
            sizeof(corrupt_data));
  fclose(f);

  // Reopen - should auto-recover by truncating corrupted tail entry
  memset(&storage, 0, sizeof(storage));
  result = storage_file_open(&storage, "test_corrupt_tail_crc.dat");
  ASSERT_EQ(result, STORAGE_OK);

  // Original chunk should still exist
  uint32_t size = sizeof(data1);
  result = storage_file_get(&storage, 111, data1, &size);
  ASSERT_EQ(result, STORAGE_OK);
  ASSERT_EQ(size, 50);

  // Corrupted chunk should NOT exist (was truncated)
  size = sizeof(corrupt_data);
  result = storage_file_get(&storage, 222, corrupt_data, &size);
  ASSERT_EQ(result, STORAGE_NOT_FOUND);

  storage_file_close(&storage);
  remove("test_corrupt_tail_crc.dat");
  return true;
}

// Test CRC corruption detection during read (file already open)
TEST(corruption_crc_on_read) {
  StorageFile storage = {0};
  StorageResult result;

  // Create file with chunk
  result = storage_file_open(&storage, "test_corrupt_read.dat");
  ASSERT_EQ(result, STORAGE_OK);

  uint8_t data1[50];
  memset(data1, 0xDD, sizeof(data1));
  result = storage_file_set(&storage, 333, data1, sizeof(data1));
  ASSERT_EQ(result, STORAGE_OK);

  // Get the file handle and corrupt the chunk data while file is still open
  // This tests CRC validation during storage_file_get(), not during scan
  FILE *f = storage.file;
  int64_t current_pos = ftell(f);

  // Seek to middle of entry data (FileHeader=8, EntryHeader=16)
  ASSERT_EQ(fseek(f, 8 + 16 + 25, SEEK_SET), 0);
  uint8_t corrupt_byte = 0x00;
  ASSERT_EQ(fwrite(&corrupt_byte, 1, 1, f), 1);

  // Restore file position
  ASSERT_EQ(fseek(f, current_pos, SEEK_SET), 0);

  // Try to read the corrupted chunk - should detect CRC mismatch
  uint8_t read_data[50];
  uint32_t size = sizeof(read_data);
  result = storage_file_get(&storage, 333, read_data, &size);
  ASSERT_EQ(result, STORAGE_CORRUPTION);

  storage_file_close(&storage);
  remove("test_corrupt_read.dat");
  return true;
}

void storage_file_run_tests(void) {
  printf("\n=== Storage File Tests ===\n\n");

  tests_run = 0;
  tests_passed = 0;
  tests_failed = 0;

  RUN_TEST(basic_operations);
  RUN_TEST(argument_validation);
  RUN_TEST(overwrite);
  RUN_TEST(persistence);
  RUN_TEST(multiple_chunks);
  RUN_TEST(compaction);
  RUN_TEST(buffer_too_small);
  RUN_TEST(large_chunk);
  RUN_TEST(delete_missing);
  RUN_TEST(reopen_after_close);
  RUN_TEST(corruption_truncated_tail);
  RUN_TEST(corruption_crc_at_tail);
  RUN_TEST(corruption_mid_file);
  RUN_TEST(corruption_crc_on_read);

  printf("\n=== Test Results ===\n");
  printf("Total:  %d\n", tests_run);
  printf("Passed: %d\n", tests_passed);
  printf("Failed: %d\n", tests_failed);

  if (tests_failed > 0) {
    printf("\nTESTS FAILED!\n");
  } else {
    printf("\nALL TESTS PASSED!\n");
  }
}

#endif // STORAGE_FILE_TESTS
