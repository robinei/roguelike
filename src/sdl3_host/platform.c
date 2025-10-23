#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "platform.h"

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

bool platform_truncate_file(FILE *f, uint64_t size) {
#ifdef _WIN32
  return _chsize_s(_fileno(f), size) == 0;
#else
  return ftruncate(fileno(f), size) == 0;
#endif
}

bool platform_atomic_replace(const char *src_path, const char *dst_path) {
#ifdef _WIN32
  return MoveFileExA(src_path, dst_path, MOVEFILE_REPLACE_EXISTING) != 0;
#else
  return rename(src_path, dst_path) == 0;
#endif
}

bool platform_fsync(FILE *f) {
  // Flush stdio buffers first
  if (fflush(f) != 0) {
    return false;
  }

#ifdef _WIN32
  // Windows: FlushFileBuffers on the handle
  HANDLE h = (HANDLE)_get_osfhandle(_fileno(f));
  if (h == INVALID_HANDLE_VALUE) {
    return false;
  }
  return FlushFileBuffers(h) != 0;
#else
  // POSIX: fsync on the file descriptor
  return fsync(fileno(f)) == 0;
#endif
}

int64_t platform_file_size(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    return -1;
  }

  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return -1;
  }

  int64_t size = ftell(f);
  fclose(f);
  return size;
}
