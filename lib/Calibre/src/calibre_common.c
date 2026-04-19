/**
 * @file calibre_common.c
 * @brief Shared implementations for Calibre wireless library
 */

#include "calibre_common.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>

#include "calibre_internal.h"

/* ============================================================================
 * Broadcast Ports Definition
 * ============================================================================ */

const uint16_t calibre_broadcast_ports[CALIBRE_BROADCAST_PORT_COUNT] = CALIBRE_BROADCAST_PORTS;

/* ============================================================================
 * Socket Utilities
 * ============================================================================ */

int calibre_socket_set_nonblocking(int sock) {
  int flags = fcntl(sock, F_GETFL, 0);
  if (flags < 0) return -1;
  return fcntl(sock, F_SETFL, flags | O_NONBLOCK);
}

int calibre_socket_set_blocking(int sock) {
  int flags = fcntl(sock, F_GETFL, 0);
  if (flags < 0) return -1;
  return fcntl(sock, F_SETFL, flags & ~O_NONBLOCK);
}

int calibre_socket_set_timeout(int sock, uint32_t timeout_ms) {
  struct timeval tv;
  tv.tv_sec = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;

  if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
    return -1;
  }
  return setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

int calibre_socket_wait_readable(int sock, uint32_t timeout_ms) {
  fd_set rfds;
  FD_ZERO(&rfds);
  FD_SET(sock, &rfds);

  struct timeval tv;
  tv.tv_sec = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;

  int ret = select(sock + 1, &rfds, NULL, NULL, &tv);
  if (ret < 0 && errno == EINTR) {
    return 0; /* Treat interrupt as timeout */
  }
  return ret;
}

uint32_t calibre_millis(void) {
#ifdef ESP_PLATFORM
  return (uint32_t)(esp_timer_get_time() / 1000);
#else
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (uint32_t)((uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000);
#endif
}

/* ============================================================================
 * JSON Helpers
 * ============================================================================ */

/**
 * @brief Decode a JSON string with escape sequences to UTF-8
 *
 * Handles: \", \\, \/, \b, \f, \n, \r, \t, and \uXXXX Unicode escapes
 *
 * @param src Source string (without quotes)
 * @param src_len Length of source string
 * @param dst Destination buffer
 * @param dst_size Size of destination buffer
 * @return Number of bytes written (excluding null terminator)
 */
static size_t json_decode_string(const char* src, size_t src_len, char* dst, size_t dst_size) {
  if (!src || !dst || dst_size == 0) return 0;

  const char* end = src + src_len;
  char* out = dst;
  char* out_end = dst + dst_size - 1; /* Leave room for null */

  while (src < end && out < out_end) {
    if (*src == '\\' && src + 1 < end) {
      src++; /* Skip backslash */
      switch (*src) {
        case '"':
        case '\\':
        case '/':
          *out++ = *src++;
          break;
        case 'b':
          *out++ = '\b';
          src++;
          break;
        case 'f':
          *out++ = '\f';
          src++;
          break;
        case 'n':
          *out++ = '\n';
          src++;
          break;
        case 'r':
          *out++ = '\r';
          src++;
          break;
        case 't':
          *out++ = '\t';
          src++;
          break;
        case 'u':
          /* Unicode escape: \uXXXX */
          if (src + 5 <= end) {
            /* Parse 4 hex digits */
            uint32_t codepoint = 0;
            bool valid = true;
            for (int i = 1; i <= 4 && valid; i++) {
              char c = src[i];
              codepoint <<= 4;
              if (c >= '0' && c <= '9')
                codepoint |= c - '0';
              else if (c >= 'a' && c <= 'f')
                codepoint |= c - 'a' + 10;
              else if (c >= 'A' && c <= 'F')
                codepoint |= c - 'A' + 10;
              else
                valid = false;
            }

            if (valid) {
              src += 5; /* Skip uXXXX */

              /* Handle surrogate pairs for characters > U+FFFF */
              if (codepoint >= 0xD800 && codepoint <= 0xDBFF && src + 6 <= end && src[0] == '\\' && src[1] == 'u') {
                /* High surrogate - look for low surrogate */
                uint32_t low = 0;
                valid = true;
                for (int i = 2; i <= 5 && valid; i++) {
                  char c = src[i];
                  low <<= 4;
                  if (c >= '0' && c <= '9')
                    low |= c - '0';
                  else if (c >= 'a' && c <= 'f')
                    low |= c - 'a' + 10;
                  else if (c >= 'A' && c <= 'F')
                    low |= c - 'A' + 10;
                  else
                    valid = false;
                }
                if (valid && low >= 0xDC00 && low <= 0xDFFF) {
                  src += 6; /* Skip \uXXXX */
                  codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
                }
              }

              /* Encode codepoint as UTF-8 */
              if (codepoint < 0x80) {
                if (out < out_end) *out++ = (char)codepoint;
              } else if (codepoint < 0x800) {
                if (out + 1 < out_end) {
                  *out++ = (char)(0xC0 | (codepoint >> 6));
                  *out++ = (char)(0x80 | (codepoint & 0x3F));
                }
              } else if (codepoint < 0x10000) {
                if (out + 2 < out_end) {
                  *out++ = (char)(0xE0 | (codepoint >> 12));
                  *out++ = (char)(0x80 | ((codepoint >> 6) & 0x3F));
                  *out++ = (char)(0x80 | (codepoint & 0x3F));
                }
              } else if (codepoint < 0x110000) {
                if (out + 3 < out_end) {
                  *out++ = (char)(0xF0 | (codepoint >> 18));
                  *out++ = (char)(0x80 | ((codepoint >> 12) & 0x3F));
                  *out++ = (char)(0x80 | ((codepoint >> 6) & 0x3F));
                  *out++ = (char)(0x80 | (codepoint & 0x3F));
                }
              }
            } else {
              /* Invalid escape, copy literally */
              if (out < out_end) *out++ = '\\';
              if (out < out_end) *out++ = 'u';
            }
          } else {
            /* Not enough chars for \uXXXX, copy literally */
            if (out < out_end) *out++ = '\\';
            if (out < out_end) *out++ = 'u';
          }
          break;
        default:
          /* Unknown escape, copy backslash and char */
          if (out < out_end) *out++ = '\\';
          if (out < out_end) *out++ = *src++;
          break;
      }
    } else {
      *out++ = *src++;
    }
  }

  *out = '\0';
  return out - dst;
}

bool calibre_json_extract_string(const char* json, size_t json_len, const char* key, char* out, size_t out_size) {
  if (!json || !key || !out || out_size == 0) {
    return false;
  }

  size_t key_len = strlen(key);
  const char* ptr = json;
  const char* end = json + json_len;

  /* Search for "key": pattern */
  while (ptr < end - key_len - 3) {
    if (*ptr == '"') {
      if (strncmp(ptr + 1, key, key_len) == 0 && ptr[key_len + 1] == '"') {
        ptr += key_len + 2; /* Skip past key and closing quote */

        /* Skip whitespace and colon */
        while (ptr < end && (*ptr == ' ' || *ptr == ':' || *ptr == '\t')) {
          ptr++;
        }

        if (ptr < end && *ptr == '"') {
          ptr++; /* Skip opening quote */
          const char* value_start = ptr;

          /* Find closing quote (handle escapes) */
          while (ptr < end) {
            if (*ptr == '\\' && ptr + 1 < end) {
              ptr += 2; /* Skip escaped char */
            } else if (*ptr == '"') {
              /* Decode the JSON string value to UTF-8 */
              json_decode_string(value_start, ptr - value_start, out, out_size);
              return true;
            } else {
              ptr++;
            }
          }
        }
      }
    }
    ptr++;
  }

  out[0] = '\0';
  return false;
}

bool calibre_json_extract_int(const char* json, size_t json_len, const char* key, int64_t* out) {
  if (!json || !key || !out) {
    return false;
  }

  size_t key_len = strlen(key);
  const char* ptr = json;
  const char* end = json + json_len;

  while (ptr < end - key_len - 3) {
    if (*ptr == '"') {
      if (strncmp(ptr + 1, key, key_len) == 0 && ptr[key_len + 1] == '"') {
        ptr += key_len + 2;

        while (ptr < end && (*ptr == ' ' || *ptr == ':' || *ptr == '\t')) {
          ptr++;
        }

        if (ptr < end && (*ptr == '-' || (*ptr >= '0' && *ptr <= '9'))) {
          errno = 0;
          char* endptr;
          int64_t val = strtoll(ptr, &endptr, 10);
          if (errno == ERANGE || endptr == ptr) {
            return false;
          }
          *out = val;
          return true;
        }
      }
    }
    ptr++;
  }
  return false;
}

/* ============================================================================
 * Response Helpers
 * ============================================================================ */

calibre_err_t calibre_send_error(calibre_conn_t* conn, const char* error_msg) {
  const char* msg = error_msg ? error_msg : "Unknown error";

  /* Escape JSON special characters in error message */
  char escaped[128];
  char* dst = escaped;
  char* dst_end = escaped + sizeof(escaped) - 1;

  while (*msg && dst < dst_end) {
    if (*msg == '"' || *msg == '\\') {
      if (dst + 1 >= dst_end) break;
      *dst++ = '\\';
    }
    *dst++ = *msg++;
  }
  *dst = '\0';

  char response[256];
  snprintf(response, sizeof(response), "{\"errorMessage\": \"%s\"}", escaped);
  return calibre_send_msg(conn, CALIBRE_OP_ERROR, response);
}

calibre_err_t calibre_send_ok(calibre_conn_t* conn, const char* json_payload) {
  const char* payload = (json_payload && json_payload[0]) ? json_payload : "{}";
  CAL_LOGI(CAL_LOG_TAG_NET, "send_ok payload: %s", payload);
  return calibre_send_msg(conn, CALIBRE_OP_OK, payload);
}

/* ============================================================================
 * Path Validation
 * ============================================================================ */

/**
 * @brief Check if path has a valid extension from config or default list
 * @param path File path to check
 * @param config Device config with accepted extensions (NULL for default)
 * @return true if extension is valid
 */
static bool has_valid_extension(const char* path, const calibre_device_config_t* config) {
  const char* ext = strrchr(path, '.');
  if (!ext) return false;

  /* Skip the dot for comparison with config extensions */
  const char* ext_no_dot = ext + 1;

  /* Use config extensions if available */
  if (config && config->extension_count > 0) {
    for (int i = 0; i < config->extension_count; i++) {
      if (strcasecmp(ext_no_dot, config->extensions[i]) == 0) {
        return true;
      }
    }
    return false;
  }

  /* Fallback to default extensions (for when no config is provided) */
  static const char* default_exts[] = {"epub", "txt", "md", "xtc", "xtch"};
  static const size_t num_exts = sizeof(default_exts) / sizeof(default_exts[0]);

  for (size_t i = 0; i < num_exts; i++) {
    if (strcasecmp(ext_no_dot, default_exts[i]) == 0) {
      return true;
    }
  }
  return false;
}

bool calibre_validate_lpath(const char* lpath, const calibre_device_config_t* config) {
  /* Null or empty check */
  if (!lpath || lpath[0] == '\0') {
    return false;
  }

  /* No absolute paths */
  if (lpath[0] == '/') {
    return false;
  }

  /* No path traversal */
  if (strstr(lpath, "..") != NULL) {
    return false;
  }

  /* Check for valid extension */
  if (!has_valid_extension(lpath, config)) {
    return false;
  }

  /* Check path length */
  if (strlen(lpath) >= CALIBRE_MAX_PATH_LEN) {
    return false;
  }

  return true;
}

bool calibre_validate_size(uint64_t size) { return size > 0 && size <= CALIBRE_MAX_BOOK_SIZE; }
