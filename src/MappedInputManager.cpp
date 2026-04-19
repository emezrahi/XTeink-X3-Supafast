#include "MappedInputManager.h"

#include "core/PixelpaperSettings.h"

decltype(InputManager::BTN_BACK) MappedInputManager::mapButton(const Button button) const {
  const auto frontLayout = settings_ ? static_cast<pixelpaper::Settings::FrontButtonLayout>(settings_->frontButtonLayout)
                                     : pixelpaper::Settings::FrontBCLR;
  const auto sideLayout = settings_ ? static_cast<pixelpaper::Settings::SideButtonLayout>(settings_->sideButtonLayout)
                                    : pixelpaper::Settings::PrevNext;

  switch (button) {
    case Button::Back:
      switch (frontLayout) {
        case pixelpaper::Settings::FrontLRBC:
          return InputManager::BTN_LEFT;
        case pixelpaper::Settings::FrontBCLR:
        default:
          return InputManager::BTN_BACK;
      }
    case Button::Confirm:
      switch (frontLayout) {
        case pixelpaper::Settings::FrontLRBC:
          return InputManager::BTN_RIGHT;
        case pixelpaper::Settings::FrontBCLR:
        default:
          return InputManager::BTN_CONFIRM;
      }
    case Button::Left:
      switch (frontLayout) {
        case pixelpaper::Settings::FrontLRBC:
          return InputManager::BTN_BACK;
        case pixelpaper::Settings::FrontBCLR:
        default:
          return InputManager::BTN_LEFT;
      }
    case Button::Right:
      switch (frontLayout) {
        case pixelpaper::Settings::FrontLRBC:
          return InputManager::BTN_CONFIRM;
        case pixelpaper::Settings::FrontBCLR:
        default:
          return InputManager::BTN_RIGHT;
      }
    case Button::Up:
      switch (sideLayout) {
        case pixelpaper::Settings::NextPrev:
          return InputManager::BTN_DOWN;
        case pixelpaper::Settings::PrevNext:
        default:
          return InputManager::BTN_UP;
      }
    case Button::Down:
      switch (sideLayout) {
        case pixelpaper::Settings::NextPrev:
          return InputManager::BTN_UP;
        case pixelpaper::Settings::PrevNext:
        default:
          return InputManager::BTN_DOWN;
      }
    case Button::Power:
      return InputManager::BTN_POWER;
    case Button::PageBack:
      switch (sideLayout) {
        case pixelpaper::Settings::NextPrev:
          return InputManager::BTN_DOWN;
        case pixelpaper::Settings::PrevNext:
        default:
          return InputManager::BTN_UP;
      }
    case Button::PageForward:
      switch (sideLayout) {
        case pixelpaper::Settings::NextPrev:
          return InputManager::BTN_UP;
        case pixelpaper::Settings::PrevNext:
        default:
          return InputManager::BTN_DOWN;
      }
  }

  return InputManager::BTN_BACK;
}

bool MappedInputManager::wasPressed(const Button button) const { return inputManager.wasPressed(mapButton(button)); }

bool MappedInputManager::wasReleased(const Button button) const { return inputManager.wasReleased(mapButton(button)); }

bool MappedInputManager::isPressed(const Button button) const { return inputManager.isPressed(mapButton(button)); }

bool MappedInputManager::wasAnyPressed() const { return inputManager.wasAnyPressed(); }

bool MappedInputManager::wasAnyReleased() const { return inputManager.wasAnyReleased(); }

unsigned long MappedInputManager::getHeldTime() const { return inputManager.getHeldTime(); }
