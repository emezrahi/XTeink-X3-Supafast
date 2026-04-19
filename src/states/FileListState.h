#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "../ui/views/SettingsViews.h"
#include "State.h"

class GfxRenderer;

namespace pixelpaper {

// FileListState - browse and select files
// Uses dynamic vector for unlimited file support with pagination
class FileListState : public State {
  enum class Screen : uint8_t {
    Browse,
    ConfirmDelete,
  };

 public:
  explicit FileListState(GfxRenderer& renderer);
  ~FileListState() override;

  void enter(Core& core) override;
  void exit(Core& core) override;
  StateTransition update(Core& core) override;
  void render(Core& core) override;
  StateId id() const override { return StateId::FileList; }

  // Get selected file path after state exits
  const char* selectedPath() const { return selectedPath_; }

  // Set initial directory before entering
  void setDirectory(const char* dir);

 private:
  GfxRenderer& renderer_;
  char currentDir_[256];
  char selectedPath_[256];

  // File entries - dynamic vector for unlimited files
  struct FileEntry {
    std::string name;
    bool isDir;
    // 0=unknown, 1=unread, 2=in-progress, 3=finished
    uint8_t readProgress = 0;
    uint8_t progressPct = 0;
  };
  std::vector<FileEntry> files_;

  size_t selectedIndex_;
  bool needsRender_;
  bool hasSelection_;
  bool firstRender_;  // Use HALF_REFRESH on first render to clear ghosting
  bool centerPressActive_;
  bool centerLongPressHandled_;
  bool ignoreCenterUntilRelease_;
  Screen currentScreen_;
  ui::ConfirmDialogView confirmView_;

  void loadFiles(Core& core);
  void promptDelete(Core& core);
  void navigateUp(Core& core);
  void navigateDown(Core& core);
  void openSelected(Core& core);
  void openLastOpened(Core& core);
  void goBack(Core& core);

  // Pagination helpers
  int getPageItems() const;
  int getTotalPages() const;
  int getCurrentPage() const;
  int getPageStartIndex() const;

  bool isHidden(const char* name) const;
  bool isSupportedFile(const char* name) const;
  bool isAtRoot() const { return strcmp(currentDir_, "/") == 0; }
};

}  // namespace pixelpaper
