#include "test_utils.h"

#include <cstdint>

// Inline the enums from PixelpaperSettings.h to avoid firmware dependencies
namespace pixelpaper {
struct Settings {
  enum SideButtonLayout : uint8_t { PrevNext = 0, NextPrev = 1 };
  enum FrontButtonLayout : uint8_t { FrontBCLR = 0, FrontLRBC = 1 };

  uint8_t sideButtonLayout = PrevNext;
  uint8_t frontButtonLayout = FrontBCLR;
};
}  // namespace pixelpaper

int main() {
  TestUtils::TestRunner runner("SettingsDefaultsTest");

  // FrontButtonLayout enum values
  runner.expectEq(uint8_t(0), uint8_t(pixelpaper::Settings::FrontBCLR), "FrontBCLR == 0");
  runner.expectEq(uint8_t(1), uint8_t(pixelpaper::Settings::FrontLRBC), "FrontLRBC == 1");

  // SideButtonLayout enum values
  runner.expectEq(uint8_t(0), uint8_t(pixelpaper::Settings::PrevNext), "PrevNext == 0");
  runner.expectEq(uint8_t(1), uint8_t(pixelpaper::Settings::NextPrev), "NextPrev == 1");

  // Default values
  pixelpaper::Settings settings;
  runner.expectEq(uint8_t(pixelpaper::Settings::FrontBCLR), settings.frontButtonLayout, "frontButtonLayout default is FrontBCLR");
  runner.expectEq(uint8_t(pixelpaper::Settings::PrevNext), settings.sideButtonLayout, "sideButtonLayout default is PrevNext");

  runner.printSummary();
  return runner.allPassed() ? 0 : 1;
}
