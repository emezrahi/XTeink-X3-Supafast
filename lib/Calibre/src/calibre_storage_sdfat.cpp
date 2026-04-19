/**
 * @file calibre_storage_sdfat.cpp
 * @brief SdFat implementation of storage abstraction
 */

#include <SDCardManager.h>
#include <SdFat.h>

#include <cstring>

#include "calibre_common.h"
#include "calibre_storage.h"

/* Log tag for this module */
#define TAG CAL_LOG_TAG_STORE

/** File handle wrapper */
struct CalibreFileHandle {
  FsFile file;
};

extern "C" {

int calibre_storage_mkdir_p(const char* path) {
  if (!SdMan.ready()) {
    CAL_LOGE(TAG, "SD card not ready");
    return -1;
  }

  // Extract directory part (everything before last '/')
  char dir_path[CALIBRE_MAX_PATH_LEN];
  strncpy(dir_path, path, sizeof(dir_path) - 1);
  dir_path[sizeof(dir_path) - 1] = '\0';

  char* last_slash = strrchr(dir_path, '/');
  if (!last_slash || last_slash == dir_path) {
    return 0;  // Root or no directory component
  }
  *last_slash = '\0';

  // Use SDCardManager's ensureDirectoryExists which handles recursive creation
  if (!SdMan.ensureDirectoryExists(dir_path)) {
    CAL_LOGE(TAG, "Failed to create directory: %s", dir_path);
    return -1;
  }

  CAL_LOGI(TAG, "Directory ready: %s", dir_path);
  return 0;
}

calibre_file_t calibre_storage_open_write(const char* path) {
  if (!SdMan.ready()) {
    CAL_LOGE(TAG, "SD card not ready");
    return nullptr;
  }

  auto* handle = new CalibreFileHandle();
  if (!handle) {
    return nullptr;
  }

  // Remove existing file first
  if (SdMan.exists(path)) {
    SdMan.remove(path);
  }

  handle->file = SdMan.open(path, O_WRONLY | O_CREAT | O_TRUNC);
  if (!handle->file.isOpen()) {
    CAL_LOGE(TAG, "Failed to open file for writing: %s", path);
    delete handle;
    return nullptr;
  }

  CAL_LOGI(TAG, "Opened file for writing: %s", path);
  return static_cast<calibre_file_t>(handle);
}

int calibre_storage_write(calibre_file_t file, const void* data, size_t len) {
  auto* handle = static_cast<CalibreFileHandle*>(file);
  if (!handle || !handle->file.isOpen()) {
    return -1;
  }

  size_t written = handle->file.write(data, len);
  if (written != len) {
    CAL_LOGE(TAG, "Write failed: wrote %zu of %zu bytes", written, len);
    return -1;
  }

  return static_cast<int>(written);
}

void calibre_storage_close(calibre_file_t file) {
  auto* handle = static_cast<CalibreFileHandle*>(file);
  if (!handle) {
    return;
  }

  if (handle->file.isOpen()) {
    handle->file.close();
  }

  delete handle;
}

int calibre_storage_unlink(const char* path) {
  if (!SdMan.ready()) {
    return -1;
  }

  if (SdMan.exists(path)) {
    if (!SdMan.remove(path)) {
      CAL_LOGE(TAG, "Failed to delete file: %s", path);
      return -1;
    }
  }

  return 0;
}

bool calibre_storage_exists(const char* path) {
  if (!SdMan.ready()) {
    return false;
  }

  return SdMan.exists(path);
}

}  // extern "C"
