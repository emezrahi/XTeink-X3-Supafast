/**
 * @file calibre_internal.h
 * @brief Internal definitions for Calibre wireless library
 *
 * This file contains private structures and functions not exposed
 * in the public API.
 */

#ifndef CALIBRE_INTERNAL_H
#define CALIBRE_INTERNAL_H

#include <netinet/in.h>
#include <sys/socket.h>

#include "calibre_common.h"
#include "calibre_wireless.h"

/* ============================================================================
 * Internal Constants
 * ============================================================================ */

/** Magic bytes for length prefix validation */
#define CALIBRE_MAX_MSG_LEN (1024 * 1024) /* 1MB max message */

/** Password hash length */
#define CALIBRE_PASSWORD_HASH_LEN 64

/** State machine states */
typedef enum {
  CALIBRE_STATE_IDLE = 0,
  CALIBRE_STATE_DISCOVERY,
  CALIBRE_STATE_CONNECTING,
  CALIBRE_STATE_HANDSHAKE,
  CALIBRE_STATE_CONNECTED,
  CALIBRE_STATE_RECEIVING_BOOK,
  CALIBRE_STATE_DISCONNECTING,
  CALIBRE_STATE_ERROR,
} calibre_state_t;

/* ============================================================================
 * Internal Structures
 * ============================================================================ */

/**
 * @brief Receive buffer for streaming data
 */
typedef struct {
  uint8_t* data;   /**< Buffer data */
  size_t capacity; /**< Buffer capacity */
  size_t len;      /**< Current data length */
  size_t pos;      /**< Current read position */
} calibre_buffer_t;

/**
 * @brief Book reception context
 */
typedef struct {
  calibre_book_meta_t meta;             /**< Book metadata */
  char temp_path[CALIBRE_MAX_PATH_LEN]; /**< Temporary file path */
  int fd;                               /**< File descriptor */
  uint64_t received;                    /**< Bytes received */
  uint64_t total;                       /**< Total bytes to receive */
} calibre_book_recv_t;

/**
 * @brief Connection context structure
 */
struct calibre_conn {
  /* Configuration */
  calibre_device_config_t config;
  calibre_callbacks_t callbacks;
  char books_dir[CALIBRE_MAX_PATH_LEN];
  char password_hash[CALIBRE_PASSWORD_HASH_LEN + 1];

  /* Network state */
  calibre_state_t state;
  int tcp_socket;        /**< Connected client socket */
  int tcp_listen_socket; /**< TCP server listening socket (legacy) */
  int udp_sockets[CALIBRE_BROADCAST_PORT_COUNT];
  uint16_t listen_port;
  struct sockaddr_in server_addr;

  /* Calibre server info (for client mode) */
  struct sockaddr_in calibre_addr; /**< Calibre server address */
  uint16_t calibre_port;           /**< Calibre server port */
  uint32_t calibre_discovered : 1; /**< Calibre was discovered via UDP */

  /* Discovery state */
  uint8_t discovery_broadcast_count; /**< Number of broadcasts sent */
  uint32_t discovery_last_broadcast; /**< Last broadcast timestamp (ms) */

  /* Protocol state */
  uint32_t msg_id;       /**< Message ID counter */
  char library_name[64]; /**< Current library name */
  char library_uuid[37]; /**< Current library UUID */

  /* Receive state */
  calibre_buffer_t recv_buf;
  calibre_book_recv_t* book_recv; /**< Active book reception */

  /* Error state */
  calibre_err_t last_error;
  char error_msg[128];

  /* Flags */
  uint32_t discovery_active : 1;
  uint32_t connected : 1;
  uint32_t cancelled : 1;
};

/* ============================================================================
 * Internal JSON Helpers (Minimal Implementation)
 * ============================================================================ */

/**
 * @brief Simple JSON value extractor (avoid external dependencies)
 */
typedef struct {
  const char* json;
  size_t len;
  size_t pos;
} json_parser_t;

/**
 * @brief Initialize JSON parser
 */
void json_parser_init(json_parser_t* p, const char* json, size_t len);

/**
 * @brief Find string value by key
 * @return Pointer to value start (excluding quotes), NULL if not found
 */
const char* json_find_string(json_parser_t* p, const char* key, size_t* out_len);

/**
 * @brief Find integer value by key
 * @return true if found, value stored in *out
 */
bool json_find_int(json_parser_t* p, const char* key, int64_t* out);

/**
 * @brief Find boolean value by key
 * @return true if found, value stored in *out
 */
bool json_find_bool(json_parser_t* p, const char* key, bool* out);

/* ============================================================================
 * Internal Protocol Functions
 * ============================================================================ */

/**
 * @brief Send a JSON message with length prefix (uses integer opcode)
 */
calibre_err_t calibre_send_msg(calibre_conn_t* conn, int opcode, const char* json_payload);

/**
 * @brief Receive and parse a JSON message (returns integer opcode)
 */
calibre_err_t calibre_recv_msg(calibre_conn_t* conn, int* opcode_out, char** json_out, uint32_t timeout_ms);

/**
 * @brief Handle GET_INITIALIZATION_INFO request
 */
calibre_err_t calibre_handle_init_info(calibre_conn_t* conn, const char* json);

/**
 * @brief Handle SET_LIBRARY_INFO message
 */
calibre_err_t calibre_handle_library_info(calibre_conn_t* conn, const char* json);

/**
 * @brief Handle FREE_SPACE request
 */
calibre_err_t calibre_handle_free_space(calibre_conn_t* conn, const char* json);

/**
 * @brief Handle SEND_BOOK request
 */
calibre_err_t calibre_handle_send_book(calibre_conn_t* conn, const char* json);

/**
 * @brief Handle SEND_BOOKLISTS request
 */
calibre_err_t calibre_handle_booklists(calibre_conn_t* conn, const char* json);

/**
 * @brief Handle DISPLAY_MESSAGE request
 */
calibre_err_t calibre_handle_message(calibre_conn_t* conn, const char* json);

/**
 * @brief Handle DELETE_BOOK request
 */
calibre_err_t calibre_handle_delete_book(calibre_conn_t* conn, const char* json);

/**
 * @brief Handle NOOP (keep-alive)
 * @note Only responds if json payload is empty; Calibre sends NOOP with
 *       wait_for_response=False when payload contains data like {"count": N}
 */
calibre_err_t calibre_handle_noop(calibre_conn_t* conn, const char* json);

/**
 * @brief Handle GET_DEVICE_INFORMATION request
 */
calibre_err_t calibre_handle_device_info(calibre_conn_t* conn, const char* json);

/**
 * @brief Handle TOTAL_SPACE request
 */
calibre_err_t calibre_handle_total_space(calibre_conn_t* conn, const char* json);

/**
 * @brief Handle GET_BOOK_COUNT request
 */
calibre_err_t calibre_handle_book_count(calibre_conn_t* conn, const char* json);

/**
 * @brief Handle SEND_BOOK_METADATA request
 */
calibre_err_t calibre_handle_book_metadata(calibre_conn_t* conn, const char* json);

/* ============================================================================
 * Internal Buffer Functions
 * ============================================================================ */

/**
 * @brief Initialize a buffer
 */
calibre_err_t calibre_buf_init(calibre_buffer_t* buf, size_t capacity);

/**
 * @brief Free buffer resources
 */
void calibre_buf_free(calibre_buffer_t* buf);

/**
 * @brief Reset buffer for reuse
 */
void calibre_buf_reset(calibre_buffer_t* buf);

/**
 * @brief Append data to buffer
 */
calibre_err_t calibre_buf_append(calibre_buffer_t* buf, const uint8_t* data, size_t len);

/* ============================================================================
 * Internal Utility Functions
 * ============================================================================ */

/**
 * @brief Generate a UUID string
 */
void calibre_gen_uuid(char* buf, size_t len);

/**
 * @brief Calculate password response hash
 */
void calibre_hash_password(const char* password, const char* challenge, char* out, size_t out_len);

/**
 * @brief Safe string copy with null termination
 */
void calibre_strlcpy(char* dst, const char* src, size_t size);

/**
 * @brief Set connection error state
 */
void calibre_set_error(calibre_conn_t* conn, calibre_err_t err, const char* msg);

/**
 * @brief Get current timestamp in ISO format
 */
void calibre_get_timestamp(char* buf, size_t len);

#endif /* CALIBRE_INTERNAL_H */
