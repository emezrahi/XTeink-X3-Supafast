#include "FileListState.h"

#include <Arduino.h>
#include <EInkDisplay.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <SDCardManager.h>
#include <Utf8.h>
#include <esp_system.h>

#include <algorithm>
#include <cctype>
#include <cstring>

#include "../core/BootMode.h"
#include "../core/Core.h"
#include "../ui/Elements.h"
#include "MappedInputManager.h"
#include "ThemeManager.h"

#define TAG "FILELIST"

namespace pixelpaper {

namespace {
constexpr const char* kLibraryRoot = "/books";

bool isUnderLibraryRoot(const char* path) {
  if (!path) return false;
  const size_t rootLen = strlen(kLibraryRoot);
  if (strncmp(path, kLibraryRoot, rootLen) != 0) return false;
  return path[rootLen] == '\0' || path[rootLen] == '/';
}
}  // namespace

FileListState::FileListState(GfxRenderer& renderer)
    : renderer_(renderer),
      selectedIndex_(0),
      needsRender_(true),
      hasSelection_(false),
      firstRender_(true),
    centerPressActive_(false),
    centerLongPressHandled_(false),
    ignoreCenterUntilRelease_(false),
      currentScreen_(Screen::Browse),
      confirmView_{} {
  strcpy(currentDir_, kLibraryRoot);
  selectedPath_[0] = '\0';
}

FileListState::~FileListState() = default;

void FileListState::setDirectory(const char* dir) {
  if (dir && dir[0] != '\0' && isUnderLibraryRoot(dir)) {
    strncpy(currentDir_, dir, sizeof(currentDir_) - 1);
    currentDir_[sizeof(currentDir_) - 1] = '\0';
  } else {
    strcpy(currentDir_, kLibraryRoot);
  }
}

void FileListState::enter(Core& core) {
  LOG_INF(TAG, "Entering, dir: %s", currentDir_);

  // In-process transitions from Reader can leave queued long-press/release
  // events behind; clear and resync so Library doesn't immediately open delete.
  core.events.clear();
  core.input.resyncState();

  // Preserve position when returning from Reader, both via boot transition and in-process transition.
  const auto& transition = getTransition();
  bool preservePosition =
      (transition.isValid() && transition.returnTo == ReturnTo::FILE_MANAGER) || core.settings.fileListDir[0] != '\0';

  if (preservePosition) {
    // Restore directory from settings
    strncpy(currentDir_, core.settings.fileListDir, sizeof(currentDir_) - 1);
    currentDir_[sizeof(currentDir_) - 1] = '\0';
  }

  if (!isUnderLibraryRoot(currentDir_)) {
    strcpy(currentDir_, kLibraryRoot);
  }

  needsRender_ = true;
  hasSelection_ = false;
  firstRender_ = true;
  centerPressActive_ = false;
  centerLongPressHandled_ = false;
  ignoreCenterUntilRelease_ = core.input.isPressed(Button::Center);
  currentScreen_ = Screen::Browse;
  selectedPath_[0] = '\0';

  loadFiles(core);

  if (preservePosition && !files_.empty()) {
    selectedIndex_ = core.settings.fileListSelectedIndex;

    // Clamp to valid range
    if (selectedIndex_ >= files_.size()) {
      selectedIndex_ = files_.size() - 1;
    }

    // Verify filename matches, search if not
    if (strcasecmp(files_[selectedIndex_].name.c_str(), core.settings.fileListSelectedName) != 0) {
      for (size_t i = 0; i < files_.size(); i++) {
        if (strcasecmp(files_[i].name.c_str(), core.settings.fileListSelectedName) == 0) {
          selectedIndex_ = i;
          break;
        }
      }
    }
  } else {
    selectedIndex_ = 0;
  }
}

void FileListState::exit(Core& core) { LOG_INF(TAG, "Exiting"); }

void FileListState::loadFiles(Core& core) {
  files_.clear();
  files_.reserve(512);  // Pre-allocate for large libraries

  FsFile dir;
  auto result = core.storage.openDir(currentDir_, dir);
  if (!result.ok()) {
    // Ensure library root exists and retry once.
    if (strcmp(currentDir_, kLibraryRoot) == 0) {
      core.storage.mkdir(kLibraryRoot);
      result = core.storage.openDir(currentDir_, dir);
    }
    if (!result.ok()) {
      LOG_ERR(TAG, "Failed to open dir: %s", currentDir_);
      return;
    }
  }

  char name[256];
  FsFile entry;

  // Collect all entries (no hard limit during collection)
  while ((entry = dir.openNextFile())) {
    entry.getName(name, sizeof(name));

    if (isHidden(name)) {
      entry.close();
      continue;
    }

    bool isDir = entry.isDirectory();
    entry.close();

    if (isDir || isSupportedFile(name)) {
      files_.push_back({std::string(name), isDir});
    }
  }
  dir.close();

  // Safety check - prevent OOM on extreme cases
  constexpr size_t MAX_ENTRIES = 1000;
  if (files_.size() > MAX_ENTRIES) {
    LOG_INF(TAG, "Warning: truncated to %zu entries", MAX_ENTRIES);
    files_.resize(MAX_ENTRIES);
    files_.shrink_to_fit();
  }

  // Sort: directories first, then natural sort (case-insensitive)
  std::sort(files_.begin(), files_.end(), [](const FileEntry& a, const FileEntry& b) {
    if (a.isDir && !b.isDir) return true;
    if (!a.isDir && b.isDir) return false;

    const char* s1 = a.name.c_str();
    const char* s2 = b.name.c_str();

    while (*s1 && *s2) {
      const auto uc = [](char c) { return static_cast<unsigned char>(c); };
      if (std::isdigit(uc(*s1)) && std::isdigit(uc(*s2))) {
        // Skip leading zeros
        while (*s1 == '0') s1++;
        while (*s2 == '0') s2++;

        // Compare by digit length first
        int len1 = 0, len2 = 0;
        while (std::isdigit(uc(s1[len1]))) len1++;
        while (std::isdigit(uc(s2[len2]))) len2++;
        if (len1 != len2) return len1 < len2;

        // Same length: compare digit by digit
        for (int i = 0; i < len1; i++) {
          if (s1[i] != s2[i]) return s1[i] < s2[i];
        }
        s1 += len1;
        s2 += len2;
      } else {
        char c1 = std::tolower(uc(*s1));
        char c2 = std::tolower(uc(*s2));
        if (c1 != c2) return c1 < c2;
        s1++;
        s2++;
      }
    }
    return *s1 == '\0' && *s2 != '\0';
  });

  LOG_INF(TAG, "Loaded %zu entries", files_.size());
}

bool FileListState::isHidden(const char* name) const {
  if (name[0] == '.') return true;
  if (FsHelpers::isHiddenFsItem(name)) return true;
  if (strncmp(name, "FOUND.", 6) == 0) return true;
  return false;
}

bool FileListState::isSupportedFile(const char* name) const {
  const char* ext = strrchr(name, '.');
  if (!ext) return false;
  ext++;  // Skip the dot

  // Case-insensitive extension check (matches ContentTypes.cpp)
  if (strcasecmp(ext, "epub") == 0) return true;
  if (strcasecmp(ext, "xtc") == 0) return true;
  if (strcasecmp(ext, "xtch") == 0) return true;
  if (strcasecmp(ext, "xtg") == 0) return true;
  if (strcasecmp(ext, "xth") == 0) return true;
  if (strcasecmp(ext, "txt") == 0) return true;
  if (strcasecmp(ext, "md") == 0) return true;
  if (strcasecmp(ext, "markdown") == 0) return true;
  if (strcasecmp(ext, "fb2") == 0) return true;
  if (strcasecmp(ext, "html") == 0) return true;
  if (strcasecmp(ext, "htm") == 0) return true;
  return false;
}

StateTransition FileListState::update(Core& core) {
  // Process input events
  Event e;
  while (core.events.pop(e)) {
    switch (e.type) {
      case EventType::ButtonRepeat:
        if (currentScreen_ != Screen::ConfirmDelete) {
          if (e.button == Button::Up)
            navigateUp(core);
          else if (e.button == Button::Down)
            navigateDown(core);
        }
        break;

      case EventType::ButtonPress:
        if (currentScreen_ == Screen::ConfirmDelete) {
          // Confirmation dialog input
          switch (e.button) {
            case Button::Up:
            case Button::Down:
              confirmView_.toggleSelection();
              needsRender_ = true;
              break;
            case Button::Center:
              if (confirmView_.isYesSelected()) {
                // Execute delete inline (like SettingsState pattern)
                const FileEntry& entry = files_[selectedIndex_];
                char pathBuf[512];  // currentDir_(256) + '/' + name(128)
                size_t dirLen = strlen(currentDir_);
                if (currentDir_[dirLen - 1] == '/') {
                  snprintf(pathBuf, sizeof(pathBuf), "%s%s", currentDir_, entry.name.c_str());
                } else {
                  snprintf(pathBuf, sizeof(pathBuf), "%s/%s", currentDir_, entry.name.c_str());
                }

                // Check if trying to delete the currently active book
                const char* activeBook = core.settings.lastBookPath;
                if (activeBook[0] != '\0' && strcmp(pathBuf, activeBook) == 0) {
                  // Cannot delete active book — just close dialog
                } else {
                  Result<void> result = entry.isDir ? core.storage.rmdir(pathBuf) : core.storage.remove(pathBuf);

                  loadFiles(core);
                  if (selectedIndex_ >= files_.size()) {
                    selectedIndex_ = files_.empty() ? 0 : files_.size() - 1;
                  }
                }
              }
              currentScreen_ = Screen::Browse;
              needsRender_ = true;
              break;
            case Button::Left:
            case Button::Right:
              confirmView_.toggleSelection();
              needsRender_ = true;
              break;
            case Button::Back:
              currentScreen_ = Screen::Browse;
              needsRender_ = true;
              break;
            default:
              break;
          }
        } else {
          // Normal browse mode
          if (ignoreCenterUntilRelease_ && e.button == Button::Center) {
            break;
          }
          switch (e.button) {
            case Button::Up:
              navigateUp(core);
              break;
            case Button::Down:
              navigateDown(core);
              break;
            case Button::Left:
              navigateUp(core);
              break;
            case Button::Right:
              navigateDown(core);
              break;
            case Button::Center:
              centerPressActive_ = true;
              centerLongPressHandled_ = false;
              break;
            case Button::Back:
              openLastOpened(core);
              break;
            case Button::Power:
              break;
          }
        }
        break;

      case EventType::ButtonLongPress:
        if (currentScreen_ == Screen::Browse && e.button == Button::Center && !ignoreCenterUntilRelease_) {
          centerLongPressHandled_ = true;
          centerPressActive_ = false;
          promptDelete(core);
        }
        break;

      case EventType::ButtonRelease:
        if (e.button == Button::Center && ignoreCenterUntilRelease_) {
          ignoreCenterUntilRelease_ = false;
          centerPressActive_ = false;
          centerLongPressHandled_ = false;
          break;
        }
        if (currentScreen_ == Screen::Browse && e.button == Button::Center) {
          if (centerPressActive_ && !centerLongPressHandled_) {
            openSelected(core);
          }
          centerPressActive_ = false;
          centerLongPressHandled_ = false;
        }
        break;

      default:
        break;
    }
  }

  // If a file was selected, transition to reader
  if (hasSelection_) {
    hasSelection_ = false;
    return StateTransition::to(StateId::Reader);
  }

  return StateTransition::stay(StateId::FileList);
}

void FileListState::render(Core& core) {
  if (!needsRender_) {
    return;
  }

  Theme& theme = THEME_MANAGER.mutableCurrent();

  if (currentScreen_ == Screen::ConfirmDelete) {
    static const char* const deleteItems[] = {"Yes", "No"};
    renderer_.clearScreen(theme.backgroundColor);

    const int menuW = 200;
    const int menuH = 2 * (theme.itemHeight + 8) + 40 + 10;
    const int menuY = (renderer_.getScreenHeight() - menuH) / 2;
    const int lineHeight = renderer_.getLineHeight(theme.uiFontId);
    const int messageY = menuY - lineHeight * 2 - 16;

    renderer_.drawCenteredText(theme.uiFontId, messageY, confirmView_.line1, theme.primaryTextBlack);
    if (confirmView_.line2[0] != '\0') {
      renderer_.drawCenteredText(theme.uiFontId, messageY + lineHeight + 4, confirmView_.line2, theme.secondaryTextBlack);
    }

    ui::popupMenu(renderer_, theme, "Delete", deleteItems, 2, confirmView_.isYesSelected() ? 0 : 1);
    ui::buttonBar(renderer_, theme, confirmView_.buttons);
    renderer_.displayBuffer(EInkDisplay::FAST_REFRESH);
    confirmView_.needsRender = false;
    needsRender_ = false;
    core.display.markDirty();
    return;
  }

  renderer_.clearScreen(theme.backgroundColor);

  // Title with page indicator
  char title[32];
  if (getTotalPages() > 1) {
    snprintf(title, sizeof(title), "Library (%d/%d)", getCurrentPage(), getTotalPages());
  } else {
    strcpy(title, "Library");
  }
  ui::title(renderer_, theme, theme.screenMarginTop, title);

  // Empty state
  if (files_.empty()) {
    renderer_.drawText(theme.uiFontId, 20, 60, "No books found", theme.primaryTextBlack);
    renderer_.displayBuffer(EInkDisplay::FAST_REFRESH);
    needsRender_ = false;
    core.display.markDirty();
    return;
  }

  // Draw current page of items
  constexpr int listStartY = 60;
  const int itemHeight = theme.itemHeight + theme.itemSpacing;
  const int pageItems = getPageItems();
  const int pageStart = getPageStartIndex();
  const int pageEnd = std::min(pageStart + pageItems, static_cast<int>(files_.size()));

  for (int i = pageStart; i < pageEnd; i++) {
    const int y = listStartY + (i - pageStart) * itemHeight;
    ui::fileEntry(renderer_, theme, y, files_[i].name.c_str(), files_[i].isDir,
                  static_cast<size_t>(i) == selectedIndex_,
                  files_[i].readProgress, files_[i].progressPct);
  }

  ui::buttonBar(renderer_, theme, "Back", "Open/Delete", "Up", "Down");

  if (firstRender_) {
    renderer_.displayBuffer(EInkDisplay::HALF_REFRESH);
    firstRender_ = false;
  } else {
    renderer_.displayBuffer(EInkDisplay::FAST_REFRESH);
  }
  needsRender_ = false;
  core.display.markDirty();
}

void FileListState::navigateUp(Core& core) {
  if (files_.empty()) return;

  if (selectedIndex_ > 0) {
    selectedIndex_--;
  } else {
    selectedIndex_ = files_.size() - 1;  // Wrap to last item
  }
  needsRender_ = true;
}

void FileListState::navigateDown(Core& core) {
  if (files_.empty()) return;

  if (selectedIndex_ + 1 < files_.size()) {
    selectedIndex_++;
  } else {
    selectedIndex_ = 0;  // Wrap to first item
  }
  needsRender_ = true;
}

void FileListState::openSelected(Core& core) {
  if (files_.empty()) {
    return;
  }

  const FileEntry& entry = files_[selectedIndex_];

  // Build full path
  size_t dirLen = strlen(currentDir_);
  if (currentDir_[dirLen - 1] == '/') {
    snprintf(selectedPath_, sizeof(selectedPath_), "%s%s", currentDir_, entry.name.c_str());
  } else {
    snprintf(selectedPath_, sizeof(selectedPath_), "%s/%s", currentDir_, entry.name.c_str());
  }

  if (entry.isDir) {
    // Enter directory
    strncpy(currentDir_, selectedPath_, sizeof(currentDir_) - 1);
    currentDir_[sizeof(currentDir_) - 1] = '\0';
    selectedIndex_ = 0;
    loadFiles(core);
    needsRender_ = true;

    // Save directory for return after mode switch
    strncpy(core.settings.fileListDir, currentDir_, sizeof(core.settings.fileListDir) - 1);
    core.settings.fileListDir[sizeof(core.settings.fileListDir) - 1] = '\0';
    core.settings.fileListSelectedName[0] = '\0';
    core.settings.fileListSelectedIndex = 0;
  } else {
    // Save position for return
    strncpy(core.settings.fileListDir, currentDir_, sizeof(core.settings.fileListDir) - 1);
    core.settings.fileListDir[sizeof(core.settings.fileListDir) - 1] = '\0';
    strncpy(core.settings.fileListSelectedName, entry.name.c_str(), sizeof(core.settings.fileListSelectedName) - 1);
    core.settings.fileListSelectedName[sizeof(core.settings.fileListSelectedName) - 1] = '\0';
    core.settings.fileListSelectedIndex = selectedIndex_;

    // Select file - transition to Reader mode via restart
    LOG_INF(TAG, "Selected: %s", selectedPath_);
    saveTransition(BootMode::READER, selectedPath_, ReturnTo::FILE_MANAGER);
    vTaskDelay(50 / portTICK_PERIOD_MS);
    ESP.restart();
  }
}

void FileListState::openLastOpened(Core& core) {
  const char* lastBook = core.settings.lastBookPath;
  if (!lastBook || lastBook[0] == '\0') {
    return;
  }

  auto existsResult = core.storage.exists(lastBook);
  if (!existsResult.ok() || !existsResult.value) {
    return;
  }

  strncpy(core.settings.fileListDir, currentDir_, sizeof(core.settings.fileListDir) - 1);
  core.settings.fileListDir[sizeof(core.settings.fileListDir) - 1] = '\0';

  LOG_INF(TAG, "Opening last book: %s", lastBook);
  saveTransition(BootMode::READER, lastBook, ReturnTo::FILE_MANAGER);
  vTaskDelay(50 / portTICK_PERIOD_MS);
  ESP.restart();
}

void FileListState::goBack(Core& core) {
  // Keep browsing inside /books only.
  if (strcmp(currentDir_, kLibraryRoot) == 0) {
    return;
  }

  // Find last slash and truncate
  char* lastSlash = strrchr(currentDir_, '/');
  if (lastSlash && lastSlash != currentDir_) {
    *lastSlash = '\0';
  } else {
    strcpy(currentDir_, kLibraryRoot);
  }

  if (!isUnderLibraryRoot(currentDir_)) {
    strcpy(currentDir_, kLibraryRoot);
  }

  selectedIndex_ = 0;
  loadFiles(core);
  needsRender_ = true;
}

void FileListState::promptDelete(Core& core) {
  if (files_.empty()) return;

  const FileEntry& entry = files_[selectedIndex_];
  const char* typeStr = entry.isDir ? "folder" : "file";

  char line1[48];
  snprintf(line1, sizeof(line1), "Delete this %s?", typeStr);

  char line2[48];
  if (entry.name.length() > 40) {
    snprintf(line2, sizeof(line2), "%.37s...", entry.name.c_str());
  } else {
    strncpy(line2, entry.name.c_str(), sizeof(line2) - 1);
    line2[sizeof(line2) - 1] = '\0';
  }

  confirmView_.setup("Confirm Delete", line1, line2);
  currentScreen_ = Screen::ConfirmDelete;
  needsRender_ = true;
}

int FileListState::getPageItems() const {
  const Theme& theme = THEME_MANAGER.current();
  constexpr int listStartY = 60;
  constexpr int bottomMargin = 70;
  const int availableHeight = renderer_.getScreenHeight() - listStartY - bottomMargin;
  const int itemHeight = theme.itemHeight + theme.itemSpacing;
  return std::max(1, availableHeight / itemHeight);
}

int FileListState::getTotalPages() const {
  if (files_.empty()) return 1;
  const int pageItems = getPageItems();
  return (static_cast<int>(files_.size()) + pageItems - 1) / pageItems;
}

int FileListState::getCurrentPage() const {
  const int pageItems = getPageItems();
  return selectedIndex_ / pageItems + 1;
}

int FileListState::getPageStartIndex() const {
  const int pageItems = getPageItems();
  return (selectedIndex_ / pageItems) * pageItems;
}

}  // namespace pixelpaper
