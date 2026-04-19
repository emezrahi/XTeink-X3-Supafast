#pragma once

#include <GfxRenderer.h>
#include <Theme.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "../Elements.h"

namespace ui {

struct CalibreView {
  static constexpr int MAX_STATUS_LEN = 64;
  static constexpr int MAX_HELP_LEN = 96;

  enum class Status : uint8_t { Waiting, Connecting, Receiving, Complete, Error };

  ButtonBar buttons{"Cancel", "", "", ""};
  char statusMsg[MAX_STATUS_LEN] = "Waiting for Calibre...";
  char helpText[MAX_HELP_LEN] = "";
  Status status = Status::Waiting;
  int32_t received = 0;
  int32_t total = 0;
  bool needsRender = true;
  bool showRestartOption = false;  // Show restart option when disconnected/error/complete

  void setWaiting() {
    status = Status::Waiting;
    strncpy(statusMsg, "Waiting for Calibre...", MAX_STATUS_LEN - 1);
    statusMsg[MAX_STATUS_LEN - 1] = '\0';
    helpText[0] = '\0';
    showRestartOption = false;
    buttons = ButtonBar{"Cancel", "", "", ""};
    needsRender = true;
  }

  void setWaitingWithIP(const char* ip) {
    status = Status::Waiting;
    snprintf(statusMsg, MAX_STATUS_LEN, "IP: %s", ip);
    strncpy(helpText, "In Calibre: Connect/share > Wireless device", MAX_HELP_LEN - 1);
    helpText[MAX_HELP_LEN - 1] = '\0';
    showRestartOption = false;
    buttons = ButtonBar{"Cancel", "", "", ""};
    needsRender = true;
  }

  void setConnecting() {
    status = Status::Connecting;
    strncpy(statusMsg, "Connecting to Calibre...", MAX_STATUS_LEN - 1);
    statusMsg[MAX_STATUS_LEN - 1] = '\0';
    helpText[0] = '\0';
    showRestartOption = false;
    buttons = ButtonBar{"Cancel", "", "", ""};
    needsRender = true;
  }

  void setReceiving(const char* filename, int recv, int tot) {
    status = Status::Receiving;
    strncpy(statusMsg, filename, MAX_STATUS_LEN - 1);
    statusMsg[MAX_STATUS_LEN - 1] = '\0';
    helpText[0] = '\0';
    received = recv;
    total = tot;
    showRestartOption = false;
    buttons = ButtonBar{"Cancel", "", "", ""};
    needsRender = true;
  }

  void setComplete(int bookCount) {
    status = Status::Complete;
    snprintf(statusMsg, MAX_STATUS_LEN, "Received %d book(s)", bookCount);
    helpText[0] = '\0';
    showRestartOption = true;
    buttons = ButtonBar{"Back", "Restart", "", ""};
    needsRender = true;
  }

  void setError(const char* msg) {
    status = Status::Error;
    strncpy(statusMsg, msg, MAX_STATUS_LEN - 1);
    statusMsg[MAX_STATUS_LEN - 1] = '\0';
    helpText[0] = '\0';
    showRestartOption = true;
    buttons = ButtonBar{"Back", "Restart", "", ""};
    needsRender = true;
  }

  void setDisconnected() {
    status = Status::Waiting;
    strncpy(statusMsg, "Disconnected. Restart?", MAX_STATUS_LEN - 1);
    statusMsg[MAX_STATUS_LEN - 1] = '\0';
    helpText[0] = '\0';
    showRestartOption = true;
    buttons = ButtonBar{"Back", "Restart", "", ""};
    needsRender = true;
  }
};

void render(const GfxRenderer& r, const Theme& t, const CalibreView& v);

}  // namespace ui
