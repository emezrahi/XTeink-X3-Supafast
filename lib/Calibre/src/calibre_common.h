/**
 * @file calibre_common.h
 * @brief Shared internal definitions for Calibre wireless library
 *
 * This file contains common macros, utilities, and declarations
 * used across all Calibre library modules.
 */

#ifndef CALIBRE_COMMON_H
#define CALIBRE_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "calibre_wireless.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Logging Macros
 * ============================================================================ */

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "rom/ets_sys.h"

#define CAL_LOG_TAG_CORE "cal_core"
#define CAL_LOG_TAG_NET "cal_net"
#define CAL_LOG_TAG_PROTO "cal_proto"
#define CAL_LOG_TAG_STORE "cal_store"

/* Module-specific logging - use ets_printf for direct UART output */
#define CAL_LOGI(tag, fmt, ...) ets_printf("[I][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define CAL_LOGW(tag, fmt, ...) ets_printf("[W][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define CAL_LOGE(tag, fmt, ...) ets_printf("[E][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define CAL_LOGD(tag, fmt, ...) ets_printf("[D][%s] " fmt "\n", tag, ##__VA_ARGS__)

#else
/* Fallback for non-ESP platforms (testing) */
#include <stdio.h>

#define CAL_LOG_TAG_CORE "cal_core"
#define CAL_LOG_TAG_NET "cal_net"
#define CAL_LOG_TAG_PROTO "cal_proto"
#define CAL_LOG_TAG_STORE "cal_store"

#define CAL_LOGI(tag, fmt, ...) \
  do {                          \
    printf("[I][%s] ", tag);    \
    printf(fmt, ##__VA_ARGS__); \
    printf("\n");               \
  } while (0)

#define CAL_LOGW(tag, fmt, ...) \
  do {                          \
    printf("[W][%s] ", tag);    \
    printf(fmt, ##__VA_ARGS__); \
    printf("\n");               \
  } while (0)

#define CAL_LOGE(tag, fmt, ...) \
  do {                          \
    printf("[E][%s] ", tag);    \
    printf(fmt, ##__VA_ARGS__); \
    printf("\n");               \
  } while (0)

#define CAL_LOGD(tag, fmt, ...) \
  do {                          \
    printf("[D][%s] ", tag);    \
    printf(fmt, ##__VA_ARGS__); \
    printf("\n");               \
  } while (0)

#endif /* ESP_PLATFORM */

/* ============================================================================
 * Broadcast Ports Array
 * ============================================================================ */

/** Single declaration of broadcast ports - defined in calibre_common.c */
extern const uint16_t calibre_broadcast_ports[CALIBRE_BROADCAST_PORT_COUNT];

/* ============================================================================
 * Socket Utility Functions
 * ============================================================================ */

/**
 * @brief Set socket to non-blocking mode
 * @param sock Socket file descriptor
 * @return 0 on success, -1 on error
 */
int calibre_socket_set_nonblocking(int sock);

/**
 * @brief Set socket to blocking mode
 * @param sock Socket file descriptor
 * @return 0 on success, -1 on error
 */
int calibre_socket_set_blocking(int sock);

/**
 * @brief Set socket receive and send timeouts
 * @param sock Socket file descriptor
 * @param timeout_ms Timeout in milliseconds
 * @return 0 on success, -1 on error
 */
int calibre_socket_set_timeout(int sock, uint32_t timeout_ms);

/**
 * @brief Wait for socket to become readable using select()
 * @param sock Socket file descriptor
 * @param timeout_ms Timeout in milliseconds
 * @return 1 if readable, 0 on timeout, -1 on error
 */
int calibre_socket_wait_readable(int sock, uint32_t timeout_ms);

/**
 * @brief Get current time in milliseconds (monotonic)
 * @return Current timestamp in milliseconds
 */
uint32_t calibre_millis(void);

/* ============================================================================
 * JSON Helper Functions
 * ============================================================================ */

/**
 * @brief Extract a JSON string value to a fixed buffer
 *
 * Safe extraction that null-terminates and bounds-checks.
 *
 * @param json JSON string to parse
 * @param json_len Length of JSON string
 * @param key Key to extract
 * @param out Output buffer
 * @param out_size Size of output buffer
 * @return true if found and extracted, false otherwise
 */
bool calibre_json_extract_string(const char* json, size_t json_len, const char* key, char* out, size_t out_size);

/**
 * @brief Extract a JSON integer value
 *
 * @param json JSON string to parse
 * @param json_len Length of JSON string
 * @param key Key to extract
 * @param out Pointer to store extracted value
 * @return true if found, false otherwise
 */
bool calibre_json_extract_int(const char* json, size_t json_len, const char* key, int64_t* out);

/* ============================================================================
 * Response Helper Functions
 * ============================================================================ */

/**
 * @brief Send an ERROR response to Calibre
 *
 * @param conn Connection handle
 * @param error_msg Error message string
 * @return CALIBRE_OK on success, error code otherwise
 */
calibre_err_t calibre_send_error(calibre_conn_t* conn, const char* error_msg);

/**
 * @brief Send an OK response to Calibre
 *
 * @param conn Connection handle
 * @param json_payload JSON payload (can be NULL for empty "{}")
 * @return CALIBRE_OK on success, error code otherwise
 */
calibre_err_t calibre_send_ok(calibre_conn_t* conn, const char* json_payload);

/* ============================================================================
 * Path Validation
 * ============================================================================ */

/** Maximum allowed book size (100MB) */
#define CALIBRE_MAX_BOOK_SIZE (100ULL * 1024 * 1024)

/**
 * @brief Validate a logical path from Calibre
 *
 * Security validation:
 * - Not empty
 * - Not absolute (no leading /)
 * - No path traversal (..)
 * - Has valid extension (from device config, or default list if config is NULL)
 *
 * @param lpath Logical path to validate
 * @param config Device config with accepted extensions (NULL for default list)
 * @return true if valid and safe, false otherwise
 */
bool calibre_validate_lpath(const char* lpath, const calibre_device_config_t* config);

/**
 * @brief Check if file size is acceptable
 *
 * @param size File size in bytes
 * @return true if acceptable, false if too large
 */
bool calibre_validate_size(uint64_t size);

#ifdef __cplusplus
}
#endif

#endif /* CALIBRE_COMMON_H */
