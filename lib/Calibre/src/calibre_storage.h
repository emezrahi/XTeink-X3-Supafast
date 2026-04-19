/**
 * @file calibre_storage.h
 * @brief Storage abstraction for Calibre library
 *
 * This module provides platform-independent file operations.
 * Implement calibre_storage.c for your platform (POSIX, SdFat, etc.)
 */

#ifndef CALIBRE_STORAGE_H
#define CALIBRE_STORAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Opaque file handle */
typedef void* calibre_file_t;

/**
 * @brief Create parent directories for a file path
 * @param path Full path to file (creates all parent directories)
 * @return 0 on success, -1 on error
 */
int calibre_storage_mkdir_p(const char* path);

/**
 * @brief Open file for writing (create/truncate)
 * @param path File path
 * @return File handle or NULL on error
 */
calibre_file_t calibre_storage_open_write(const char* path);

/**
 * @brief Write data to file
 * @param file File handle
 * @param data Data to write
 * @param len Length of data
 * @return Bytes written, or -1 on error
 */
int calibre_storage_write(calibre_file_t file, const void* data, size_t len);

/**
 * @brief Close file
 * @param file File handle
 */
void calibre_storage_close(calibre_file_t file);

/**
 * @brief Delete file
 * @param path File path
 * @return 0 on success, -1 on error
 */
int calibre_storage_unlink(const char* path);

/**
 * @brief Check if path exists
 * @param path Path to check
 * @return true if exists
 */
bool calibre_storage_exists(const char* path);

#ifdef __cplusplus
}
#endif

#endif /* CALIBRE_STORAGE_H */
