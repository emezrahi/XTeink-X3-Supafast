/**
 * @file calibre_network.c
 * @brief Network handling for Calibre Wireless protocol
 *
 * Implements UDP discovery and TCP connection management
 */

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "calibre_internal.h"
#include "calibre_wireless.h"

#ifdef ESP_PLATFORM
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#else
#include <netdb.h>
#endif

/* Log tag for this module */
#define TAG CAL_LOG_TAG_NET

/* ============================================================================
 * UDP Discovery
 * ============================================================================ */

calibre_err_t calibre_start_discovery(calibre_conn_t* conn, uint16_t port) {
  if (!conn) {
    return CALIBRE_ERR_INVALID_ARG;
  }

  if (conn->discovery_active) {
    return CALIBRE_OK; /* Already running */
  }

  conn->listen_port = port ? port : CALIBRE_DEFAULT_PORT;

  /* Reset discovery state */
  conn->calibre_discovered = 0;
  memset(&conn->calibre_addr, 0, sizeof(conn->calibre_addr));
  conn->calibre_port = 0;

  /* Create UDP sockets for each broadcast port
   * We need to both broadcast "hello" and listen for Calibre's response */
  for (int i = 0; i < CALIBRE_BROADCAST_PORT_COUNT; i++) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
      CAL_LOGE(TAG, "Failed to create UDP socket: %s", strerror(errno));
      continue;
    }

    /* Allow address reuse and broadcasting */
    int opt = 1;
    (void)setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    (void)setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt));

    /* Bind to any available port to receive responses */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(0); /* Let system assign port */

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
      CAL_LOGW(TAG, "Failed to bind UDP socket: %s", strerror(errno));
      close(sock);
      continue;
    }

    /* Set non-blocking */
    calibre_socket_set_nonblocking(sock);

    conn->udp_sockets[i] = sock;
    CAL_LOGD(TAG, "UDP socket %d created for discovery", i);
  }

  /* Check if we created at least one socket */
  int created_count = 0;
  for (int i = 0; i < CALIBRE_BROADCAST_PORT_COUNT; i++) {
    if (conn->udp_sockets[i] >= 0) created_count++;
  }

  if (created_count == 0) {
    calibre_set_error(conn, CALIBRE_ERR_SOCKET, "Failed to create any UDP socket");
    return CALIBRE_ERR_SOCKET;
  }

  conn->discovery_active = 1;
  conn->state = CALIBRE_STATE_DISCOVERY;
  conn->discovery_broadcast_count = 0; /* Reset broadcast counter */
  conn->discovery_last_broadcast = 0;  /* Force immediate broadcast */
  CAL_LOGI(TAG, "Discovery started, will broadcast 'hello' on %d ports", created_count);

  return CALIBRE_OK;
}

void calibre_stop_discovery(calibre_conn_t* conn) {
  if (!conn) return;

  for (int i = 0; i < CALIBRE_BROADCAST_PORT_COUNT; i++) {
    if (conn->udp_sockets[i] >= 0) {
      close(conn->udp_sockets[i]);
      conn->udp_sockets[i] = -1;
    }
  }

  conn->discovery_active = 0;
  if (conn->state == CALIBRE_STATE_DISCOVERY) {
    conn->state = CALIBRE_STATE_IDLE;
  }
  CAL_LOGI(TAG, "Discovery stopped");
}

/**
 * @brief Process UDP discovery messages
 *
 * Device broadcasts "hello" message, Calibre responds with its info.
 * We then connect to Calibre as a TCP client.
 *
 * Calibre response format: "calibre wireless device client (on <hostname>);<content_port>,<smart_device_port>"
 */
static calibre_err_t calibre_process_discovery(calibre_conn_t* conn) {
  char buf[256];
  struct sockaddr_in from_addr;
  socklen_t addr_len = sizeof(from_addr);

  /* Broadcast "hello" periodically (every 500ms) */
  uint32_t now = calibre_millis();
  if (!conn->calibre_discovered && conn->discovery_broadcast_count < CALIBRE_MAX_DISCOVERY_BROADCASTS) {
    if (now - conn->discovery_last_broadcast >= 500) {
      const char* hello_msg = "hello";

      for (int i = 0; i < CALIBRE_BROADCAST_PORT_COUNT; i++) {
        if (conn->udp_sockets[i] < 0) continue;

        struct sockaddr_in broadcast_addr;
        memset(&broadcast_addr, 0, sizeof(broadcast_addr));
        broadcast_addr.sin_family = AF_INET;
        broadcast_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
        broadcast_addr.sin_port = htons(calibre_broadcast_ports[i]);

        ssize_t sent = sendto(conn->udp_sockets[i], hello_msg, strlen(hello_msg), 0, (struct sockaddr*)&broadcast_addr,
                              sizeof(broadcast_addr));
        if (sent < 0) {
          CAL_LOGW(TAG, "Failed to broadcast 'hello' on port %d: %s", calibre_broadcast_ports[i], strerror(errno));
        }
      }

      conn->discovery_last_broadcast = now;
      conn->discovery_broadcast_count++;
      CAL_LOGD(TAG, "Broadcast 'hello' (%d/20)", conn->discovery_broadcast_count);
    }
  }

  /* Listen for responses from Calibre */
  for (int i = 0; i < CALIBRE_BROADCAST_PORT_COUNT; i++) {
    if (conn->udp_sockets[i] < 0) continue;

    ssize_t len =
        recvfrom(conn->udp_sockets[i], buf, sizeof(buf) - 1, MSG_DONTWAIT, (struct sockaddr*)&from_addr, &addr_len);

    if (len > 0) {
      buf[len] = '\0';
      CAL_LOGI(TAG, "UDP received from %s:%d: %s", inet_ntoa(from_addr.sin_addr), ntohs(from_addr.sin_port), buf);

      /* Check for Calibre response message containing "calibre" */
      if (strstr(buf, "calibre")) {
        /* Parse Calibre's port from the message */
        /* Format: "calibre wireless device client (on <hostname>);<content_port>,<smart_device_port>" */
        uint16_t calibre_port = CALIBRE_DEFAULT_PORT; /* Default port */

        /* Try to extract port from message - look for the last comma */
        char* port_ptr = strrchr(buf, ',');
        if (port_ptr) {
          char* endptr = NULL;
          long parsed_port = strtol(port_ptr + 1, &endptr, 10);
          if (endptr != port_ptr + 1 && parsed_port > 0 && parsed_port <= 65535) {
            calibre_port = (uint16_t)parsed_port;
          }
        }

        /* Store Calibre's address for connection */
        memcpy(&conn->calibre_addr, &from_addr, sizeof(from_addr));
        conn->calibre_port = calibre_port;
        conn->calibre_discovered = 1;

        CAL_LOGI(TAG, "Calibre discovered at %s:%d, ready to connect", inet_ntoa(from_addr.sin_addr), calibre_port);
      }
    }
  }

  return CALIBRE_OK;
}

/* ============================================================================
 * TCP Connection
 * ============================================================================ */

calibre_err_t calibre_connect(calibre_conn_t* conn, const char* host, uint16_t port) {
  if (!conn || !host) {
    return CALIBRE_ERR_INVALID_ARG;
  }

  if (conn->connected) {
    calibre_disconnect(conn);
  }

  CAL_LOGI(TAG, "Connecting to %s:%d", host, port);

  /* Create TCP socket */
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    calibre_set_error(conn, CALIBRE_ERR_SOCKET, strerror(errno));
    return CALIBRE_ERR_SOCKET;
  }

  /* Set timeouts */
  calibre_socket_set_timeout(sock, CALIBRE_CONNECT_TIMEOUT_MS);

  /* Resolve hostname using getaddrinfo (modern, thread-safe) */
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);

  if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
    /* Try DNS resolution with getaddrinfo */
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int ret = getaddrinfo(host, NULL, &hints, &res);
    if (ret != 0 || !res) {
      if (res) freeaddrinfo(res);
      close(sock);
      calibre_set_error(conn, CALIBRE_ERR_CONNECT, "DNS resolution failed");
      return CALIBRE_ERR_CONNECT;
    }

    /* Copy the resolved address */
    struct sockaddr_in* resolved = (struct sockaddr_in*)res->ai_addr;
    addr.sin_addr = resolved->sin_addr;

    /* Free the addrinfo result */
    freeaddrinfo(res);
  }

  /* Connect */
  if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    close(sock);
    calibre_set_error(conn, CALIBRE_ERR_CONNECT, strerror(errno));
    return CALIBRE_ERR_CONNECT;
  }

  /* Disable Nagle's algorithm for low latency (ignore failure - not critical) */
  int nodelay = 1;
  (void)setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

  /* Set receive timeout for normal operation */
  calibre_socket_set_timeout(sock, CALIBRE_RECV_TIMEOUT_MS);

  conn->tcp_socket = sock;
  conn->server_addr = addr;
  conn->state = CALIBRE_STATE_HANDSHAKE;

  CAL_LOGI(TAG, "TCP connected to %s:%d", host, port);
  return CALIBRE_OK;
}

calibre_err_t calibre_connect_to_discovered(calibre_conn_t* conn) {
  if (!conn) {
    return CALIBRE_ERR_INVALID_ARG;
  }

  if (!conn->calibre_discovered) {
    calibre_set_error(conn, CALIBRE_ERR_CONNECT, "Calibre not discovered yet");
    return CALIBRE_ERR_CONNECT;
  }

  if (conn->connected || conn->tcp_socket >= 0) {
    return CALIBRE_OK; /* Already connected */
  }

  /* Extract IP address from discovered address */
  char ip_str[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &conn->calibre_addr.sin_addr, ip_str, sizeof(ip_str));

  CAL_LOGI(TAG, "Connecting to discovered Calibre at %s:%d", ip_str, conn->calibre_port);

  return calibre_connect(conn, ip_str, conn->calibre_port);
}

void calibre_disconnect(calibre_conn_t* conn) {
  if (!conn) return;

  if (conn->tcp_socket >= 0) {
    close(conn->tcp_socket);
    conn->tcp_socket = -1;
  }

  conn->connected = 0;
  conn->state = CALIBRE_STATE_IDLE;
  calibre_buf_reset(&conn->recv_buf);

  CAL_LOGI(TAG, "Disconnected");
}

/* ============================================================================
 * Message Protocol
 * ============================================================================ */

/**
 * @brief Send raw bytes over TCP
 */
static calibre_err_t tcp_send_all(calibre_conn_t* conn, const void* data, size_t len) {
  const uint8_t* ptr = (const uint8_t*)data;
  size_t remaining = len;

  while (remaining > 0) {
    ssize_t sent = send(conn->tcp_socket, ptr, remaining, 0);
    if (sent < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        continue;
      }
      calibre_set_error(conn, CALIBRE_ERR_SOCKET, strerror(errno));
      return CALIBRE_ERR_SOCKET;
    }
    if (sent == 0) {
      calibre_set_error(conn, CALIBRE_ERR_DISCONNECTED, "Connection closed");
      return CALIBRE_ERR_DISCONNECTED;
    }
    ptr += sent;
    remaining -= sent;
  }

  return CALIBRE_OK;
}

/**
 * @brief Receive exact number of bytes with timeout
 *
 * Uses calibre_millis() for deadline tracking and calibre_socket_wait_readable()
 * for efficient socket polling.
 *
 * Note: uint32_t subtraction handles timer wraparound correctly for timeouts
 * under ~24 days (2^31 ms). Device uptime beyond 49 days would wrap calibre_millis()
 * but subtraction still yields correct elapsed time due to unsigned arithmetic.
 */
static calibre_err_t tcp_recv_exact(calibre_conn_t* conn, void* data, size_t len, uint32_t timeout_ms) {
  uint8_t* ptr = (uint8_t*)data;
  size_t remaining = len;
  uint32_t start_ms = calibre_millis();

  while (remaining > 0) {
    if (conn->cancelled) {
      return CALIBRE_ERR_CANCELLED;
    }

    /* Check deadline - unsigned subtraction handles wraparound correctly */
    uint32_t elapsed = calibre_millis() - start_ms;
    if (elapsed >= timeout_ms) {
      return CALIBRE_ERR_TIMEOUT;
    }

    /* Wait for data with remaining timeout */
    uint32_t remaining_ms = timeout_ms - elapsed;
    int ret = calibre_socket_wait_readable(conn->tcp_socket, remaining_ms);
    if (ret < 0) {
      calibre_set_error(conn, CALIBRE_ERR_SOCKET, strerror(errno));
      return CALIBRE_ERR_SOCKET;
    }
    if (ret == 0) {
      return CALIBRE_ERR_TIMEOUT;
    }

    /* Read available data */
    ssize_t received = recv(conn->tcp_socket, ptr, remaining, 0);
    if (received < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
        continue;
      }
      calibre_set_error(conn, CALIBRE_ERR_SOCKET, strerror(errno));
      return CALIBRE_ERR_SOCKET;
    }
    if (received == 0) {
      calibre_set_error(conn, CALIBRE_ERR_DISCONNECTED, "Connection closed");
      return CALIBRE_ERR_DISCONNECTED;
    }

    ptr += received;
    remaining -= received;
  }

  return CALIBRE_OK;
}

calibre_err_t calibre_send_msg(calibre_conn_t* conn, int opcode, const char* json_payload) {
  if (!conn || conn->tcp_socket < 0) {
    return CALIBRE_ERR_INVALID_ARG;
  }

  /* Build message: [opcode, payload] as JSON array - opcode is INTEGER */
  char msg_buf[CALIBRE_JSON_BUF_SIZE];
  int msg_len;

  if (json_payload && json_payload[0]) {
    msg_len = snprintf(msg_buf, sizeof(msg_buf), "[%d, %s]", opcode, json_payload);
  } else {
    msg_len = snprintf(msg_buf, sizeof(msg_buf), "[%d, {}]", opcode);
  }

  if (msg_len < 0 || msg_len >= (int)sizeof(msg_buf)) {
    calibre_set_error(conn, CALIBRE_ERR_NOMEM, "Message too large");
    return CALIBRE_ERR_NOMEM;
  }

  /* Send length prefix as ASCII decimal */
  char len_prefix[16];
  int prefix_len = snprintf(len_prefix, sizeof(len_prefix), "%d", msg_len);

  CAL_LOGI(TAG, "Sending: %s%s", len_prefix, msg_buf);

  calibre_err_t err = tcp_send_all(conn, len_prefix, prefix_len);
  if (err != CALIBRE_OK) return err;

  return tcp_send_all(conn, msg_buf, msg_len);
}

calibre_err_t calibre_recv_msg(calibre_conn_t* conn, int* opcode_out, char** json_out, uint32_t timeout_ms) {
  if (!conn || conn->tcp_socket < 0) {
    return CALIBRE_ERR_INVALID_ARG;
  }

  /* Read length prefix (ASCII decimal terminated by non-digit) */
  char len_buf[16];
  size_t len_pos = 0;

  while (len_pos < sizeof(len_buf) - 1) {
    calibre_err_t err = tcp_recv_exact(conn, &len_buf[len_pos], 1, timeout_ms);
    if (err != CALIBRE_OK) return err;

    if (len_buf[len_pos] < '0' || len_buf[len_pos] > '9') {
      /* Non-digit means we have the full length, put back the char */
      break;
    }
    len_pos++;
  }

  /* Save the first message byte before null-terminating length string */
  const char first_msg_byte = len_buf[len_pos];
  len_buf[len_pos] = '\0';
  size_t msg_len = strtoul(len_buf, NULL, 10);

  if (msg_len == 0 || msg_len > CALIBRE_MAX_MSG_LEN) {
    calibre_set_error(conn, CALIBRE_ERR_PROTOCOL, "Invalid message length");
    return CALIBRE_ERR_PROTOCOL;
  }

  /* Ensure buffer is large enough */
  if (msg_len >= conn->recv_buf.capacity) {
    /* Reallocate buffer if needed */
    calibre_buf_free(&conn->recv_buf);
    if (calibre_buf_init(&conn->recv_buf, msg_len + 1) != CALIBRE_OK) {
      return CALIBRE_ERR_NOMEM;
    }
  }

  /* Verify buffer allocation succeeded */
  if (!conn->recv_buf.data) {
    calibre_set_error(conn, CALIBRE_ERR_NOMEM, "Receive buffer allocation failed");
    return CALIBRE_ERR_NOMEM;
  }

  calibre_buf_reset(&conn->recv_buf);

  /* We already read the first byte of the message (the '[') */
  conn->recv_buf.data[0] = (uint8_t)first_msg_byte;

  /* Read rest of message */
  calibre_err_t err = tcp_recv_exact(conn, conn->recv_buf.data + 1, msg_len - 1, timeout_ms);
  if (err != CALIBRE_OK) return err;

  conn->recv_buf.data[msg_len] = '\0';
  conn->recv_buf.len = msg_len;

  CAL_LOGI(TAG, "Received: %s", (char*)conn->recv_buf.data);

  /* Parse JSON array: [opcode, {...}] - opcode is INTEGER */
  char* json = (char*)conn->recv_buf.data;

  /* Find first integer (opcode) - format is [123, {...}] */
  /* Skip the opening bracket */
  char* ptr = json + 1;
  while (*ptr && (*ptr == ' ' || *ptr == '\t')) ptr++;

  /* Parse integer opcode */
  if (*ptr < '0' || *ptr > '9') {
    calibre_set_error(conn, CALIBRE_ERR_JSON_PARSE, "Missing or invalid opcode");
    return CALIBRE_ERR_JSON_PARSE;
  }

  long opcode_long = strtol(ptr, &ptr, 10);
  if (opcode_long < 0 || opcode_long > 255) {
    calibre_set_error(conn, CALIBRE_ERR_JSON_PARSE, "Opcode out of range");
    return CALIBRE_ERR_JSON_PARSE;
  }
  if (opcode_out) {
    *opcode_out = (int)opcode_long;
  }

  /* Find payload (after comma and whitespace) */
  if (json_out) {
    while (*ptr && (*ptr == ',' || *ptr == ' ' || *ptr == '\t')) {
      ptr++;
    }
    *json_out = ptr;
  }

  return CALIBRE_OK;
}

/* ============================================================================
 * Main Processing Loop
 * ============================================================================ */

calibre_err_t calibre_process(calibre_conn_t* conn, uint32_t timeout_ms) {
  if (!conn) {
    return CALIBRE_ERR_INVALID_ARG;
  }

  calibre_err_t err = CALIBRE_OK;

  /* Process UDP discovery */
  if (conn->discovery_active) {
    calibre_process_discovery(conn);

    /* If Calibre was discovered and we're not connected, connect to it */
    if (conn->calibre_discovered && conn->tcp_socket < 0) {
      err = calibre_connect_to_discovered(conn);
      if (err == CALIBRE_OK) {
        /* Stop discovery once connected */
        for (int i = 0; i < CALIBRE_BROADCAST_PORT_COUNT; i++) {
          if (conn->udp_sockets[i] >= 0) {
            close(conn->udp_sockets[i]);
            conn->udp_sockets[i] = -1;
          }
        }
        conn->discovery_active = 0;
        CAL_LOGI(TAG, "Connected to Calibre, discovery stopped");
      } else if (err != CALIBRE_ERR_CONNECT) {
        /* Non-connection errors are serious */
        return err;
      }
      err = CALIBRE_OK;
    }
  }

  /* Process TCP connection */
  if (conn->tcp_socket >= 0 && conn->state >= CALIBRE_STATE_HANDSHAKE) {
    /* Check for incoming data using shared helper */
    int ret = calibre_socket_wait_readable(conn->tcp_socket, timeout_ms);

    if (ret > 0) {
      /* Data available, receive message */
      int opcode = -1;
      char* json = NULL;

      err = calibre_recv_msg(conn, &opcode, &json, timeout_ms ? timeout_ms : CALIBRE_RECV_TIMEOUT_MS);

      if (err != CALIBRE_OK) {
        if (err == CALIBRE_ERR_DISCONNECTED) {
          conn->connected = 0;
          conn->state = CALIBRE_STATE_IDLE;
        }
        return err;
      }

      /* Dispatch to appropriate handler using integer opcodes */
      switch (opcode) {
        case CALIBRE_OP_GET_INITIALIZATION_INFO:
          err = calibre_handle_init_info(conn, json);
          break;
        case CALIBRE_OP_GET_DEVICE_INFORMATION:
          err = calibre_handle_device_info(conn, json);
          break;
        case CALIBRE_OP_SET_CALIBRE_DEVICE_INFO:
        case CALIBRE_OP_SET_CALIBRE_DEVICE_NAME:
          /* Simple acknowledgment */
          err = calibre_send_msg(conn, CALIBRE_OP_OK, "{}");
          break;
        case CALIBRE_OP_SET_LIBRARY_INFO:
          err = calibre_handle_library_info(conn, json);
          break;
        case CALIBRE_OP_TOTAL_SPACE:
          err = calibre_handle_total_space(conn, json);
          break;
        case CALIBRE_OP_FREE_SPACE:
          err = calibre_handle_free_space(conn, json);
          break;
        case CALIBRE_OP_GET_BOOK_COUNT:
          err = calibre_handle_book_count(conn, json);
          break;
        case CALIBRE_OP_SEND_BOOK:
          err = calibre_handle_send_book(conn, json);
          break;
        case CALIBRE_OP_SEND_BOOKLISTS:
          err = calibre_handle_booklists(conn, json);
          break;
        case CALIBRE_OP_SEND_BOOK_METADATA:
          err = calibre_handle_book_metadata(conn, json);
          break;
        case CALIBRE_OP_DISPLAY_MESSAGE:
          err = calibre_handle_message(conn, json);
          break;
        case CALIBRE_OP_DELETE_BOOK:
          err = calibre_handle_delete_book(conn, json);
          break;
        case CALIBRE_OP_NOOP:
          err = calibre_handle_noop(conn, json);
          break;
        case CALIBRE_OP_OK:
          /* Server acknowledged our message */
          CAL_LOGD(TAG, "Server acknowledged");
          break;
        default:
          CAL_LOGW(TAG, "Unknown opcode: %d", opcode);
          break;
      }
    } else if (ret < 0) {
      calibre_set_error(conn, CALIBRE_ERR_SOCKET, strerror(errno));
      return CALIBRE_ERR_SOCKET;
    }
  }

  return err;
}
