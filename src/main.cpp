#include <Arduino.h>
#include <LittleFS.h>  // Must be before SdFat includes to avoid FILE_READ/FILE_WRITE redefinition
#include <EInkDisplay.h>
#include <X3Detect.h>
#include "GpioUtils.h"
#include <Epub.h>
#include <GfxRenderer.h>
#include <InputManager.h>
#include <SDCardManager.h>
#include <SPI.h>
#include <builtinFonts/atkinson_hyperlegible_mono_bold_14_2b.h>
#include <builtinFonts/atkinson_hyperlegible_mono_bold_16_2b.h>
#include <builtinFonts/atkinson_hyperlegible_mono_bold_18_2b.h>
#include <builtinFonts/atkinson_hyperlegible_mono_bold_12_2b.h>
#include <builtinFonts/atkinson_hyperlegible_mono_italic_14_2b.h>
#include <builtinFonts/atkinson_hyperlegible_mono_italic_16_2b.h>
#include <builtinFonts/atkinson_hyperlegible_mono_italic_18_2b.h>
#include <builtinFonts/atkinson_hyperlegible_mono_italic_12_2b.h>
#include <builtinFonts/atkinson_hyperlegible_mono_regular_14_2b.h>
#include <builtinFonts/atkinson_hyperlegible_mono_regular_16_2b.h>
#include <builtinFonts/atkinson_hyperlegible_mono_regular_18_2b.h>
#include <builtinFonts/atkinson_hyperlegible_mono_regular_12_2b.h>
#include <driver/gpio.h>
#include <esp_system.h>
#include <Logging.h>
#include <builtinFonts/small14.h>
#include <builtinFonts/ui_12.h>
#include <builtinFonts/ui_bold_12.h>

#include "Battery.h"
#include "FontManager.h"
#include "MappedInputManager.h"
#include "ThemeManager.h"
#include "config.h"
#include "content/ContentTypes.h"
#include "ui/Elements.h"

#define TAG "MAIN"

// New refactored core system
#include "core/BootMode.h"
#include "core/Core.h"
#include "core/StateMachine.h"
#include "images/PixelpaperLogo.h"
#include "states/AppLauncherState.h"
#include "states/CalibreSyncState.h"
#include "states/ErrorState.h"
#include "states/FileListState.h"
#include "states/NetworkState.h"
#include "states/ReaderState.h"
#include "states/SettingsState.h"
#include "states/SleepState.h"
#include "states/StartupState.h"
#include "ui/views/BootSleepViews.h"

#define SPI_FQ 40000000
// Display SPI pins (custom pins for XteinkX4, not hardware SPI defaults)
#define EPD_SCLK 8   // SPI Clock
#define EPD_MOSI 10  // SPI MOSI (Master Out Slave In)
#define EPD_CS 21    // Chip Select
#define EPD_DC 4     // Data/Command
#define EPD_RST 5    // Reset
#define EPD_BUSY 6   // Busy

#define UART0_RXD 20  // Used for USB connection detection

#define SD_SPI_MISO 7

#define SERIAL_INIT_DELAY_MS 10
#define SERIAL_READY_TIMEOUT_MS 3000

EInkDisplay einkDisplay(EPD_SCLK, EPD_MOSI, EPD_CS, EPD_DC, EPD_RST, EPD_BUSY);
InputManager inputManager;
MappedInputManager mappedInputManager(inputManager);
GfxRenderer renderer(einkDisplay);

// Extern references for driver wrappers
EInkDisplay& display = einkDisplay;
MappedInputManager& mappedInput = mappedInputManager;

// Core system
namespace pixelpaper {
Core core;
}

// State instances (pre-allocated, no heap per transition)
static pixelpaper::StartupState startupState;
static pixelpaper::FileListState fileListState(renderer);
static pixelpaper::ReaderState readerState(renderer);
static pixelpaper::SettingsState settingsState(renderer);
static pixelpaper::NetworkState networkState(renderer);
static pixelpaper::CalibreSyncState calibreSyncState(renderer);
static pixelpaper::AppLauncherState appLauncherState(renderer);
static pixelpaper::SleepState sleepState(renderer);
static pixelpaper::ErrorState errorState(renderer);
static pixelpaper::StateMachine stateMachine;

RTC_DATA_ATTR uint16_t rtcPowerButtonDurationMs = 400;

// Always-needed fonts (UI, status bar)
EpdFont smallFont(&small14);
EpdFontFamily smallFontFamily(&smallFont);

EpdFont ui12Font(&ui_12);
EpdFont uiBold12Font(&ui_bold_12);
EpdFontFamily uiFontFamily(&ui12Font, &uiBold12Font);

// Reader font families — lazily constructed via static locals so only the
// active size allocates EpdFont objects (~520 bytes each × 3 per size).
// In READER mode this saves ~4.5KB by not instantiating unused sizes.
static EpdFontFamily& readerFontFamilyXSmall() {
  static EpdFont r(&atkinson_hyperlegible_mono_regular_12), b(&atkinson_hyperlegible_mono_bold_12),
      i(&atkinson_hyperlegible_mono_italic_12);
  static EpdFontFamily f(&r, &b, &i, &b);
  return f;
}
static EpdFontFamily& readerFontFamilySmall() {
  static EpdFont r(&atkinson_hyperlegible_mono_regular_14), b(&atkinson_hyperlegible_mono_bold_14),
      i(&atkinson_hyperlegible_mono_italic_14);
  static EpdFontFamily f(&r, &b, &i, &b);
  return f;
}
static EpdFontFamily& readerFontFamilyMedium() {
  static EpdFont r(&atkinson_hyperlegible_mono_regular_16), b(&atkinson_hyperlegible_mono_bold_16),
      i(&atkinson_hyperlegible_mono_italic_16);
  static EpdFontFamily f(&r, &b, &i, &b);
  return f;
}
static EpdFontFamily& readerFontFamilyLarge() {
  static EpdFont r(&atkinson_hyperlegible_mono_regular_18), b(&atkinson_hyperlegible_mono_bold_18),
      i(&atkinson_hyperlegible_mono_italic_18);
  static EpdFontFamily f(&r, &b, &i, &b);
  return f;
}

bool isUsbConnected() {
  if (X3Detect::isX3()) return X3Detect::readIsCharging();
  return digitalRead(UART0_RXD) == HIGH;
}

struct WakeupInfo {
  esp_reset_reason_t resetReason;
  bool isPowerButton;
};

WakeupInfo getWakeupInfo() {
  const bool usbConnected = isUsbConnected();
  const auto wakeupCause = esp_sleep_get_wakeup_cause();
  const auto resetReason = esp_reset_reason();

  // Without USB: power button triggers a full power-on reset (not GPIO wakeup)
  // With USB: power button wakes from deep sleep via GPIO
  const bool isPowerButton =
      (!usbConnected && wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_POWERON) ||
      (usbConnected && wakeupCause == ESP_SLEEP_WAKEUP_GPIO && resetReason == ESP_RST_DEEPSLEEP);

  return {resetReason, isPowerButton};
}

// Verify long press on wake-up from deep sleep
void verifyWakeupLongPress(esp_reset_reason_t resetReason) {
  if (resetReason == ESP_RST_SW) {
    LOG_DBG(TAG, "Skipping wakeup verification (software restart)");
    return;
  }

  // Fast path for short press mode - skip verification entirely.
  // Uses settings directly (not RTC variable) so it works even after a full power cycle
  // where RTC memory is lost. Needed because inputManager.isPressed() may take up to
  // ~500ms to return the correct state after wake-up.
  if (pixelpaper::core.settings.shortPwrBtn == pixelpaper::Settings::PowerSleep) {
    LOG_DBG(TAG, "Skipping wakeup verification (short press mode)");
    return;
  }

  // Give the user up to 1000ms to start holding the power button, and must hold for the configured duration
  const auto start = millis();
  bool abort = false;
  const uint16_t requiredPressDuration = pixelpaper::core.settings.getPowerButtonDuration();

  inputManager.update();
  // Verify the user has actually pressed
  while (!inputManager.isPressed(InputManager::BTN_POWER) && millis() - start < 1000) {
    delay(10);  // only wait 10ms each iteration to not delay too much in case of short configured duration.
    inputManager.update();
  }

  if (inputManager.isPressed(InputManager::BTN_POWER)) {
    do {
      delay(10);
      inputManager.update();
    } while (inputManager.isPressed(InputManager::BTN_POWER) && inputManager.getHeldTime() < requiredPressDuration);
    abort = inputManager.getHeldTime() < requiredPressDuration;
  } else {
    abort = true;
  }

  if (abort) {
    // Button released too early. Returning to sleep.
    // IMPORTANT: Re-arm the wakeup trigger before sleeping again
    esp_deep_sleep_enable_gpio_wakeup(1ULL << InputManager::POWER_BUTTON_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);
    disableGpioPullsForSleep();
    esp_deep_sleep_start();
  }
}

void waitForPowerRelease() {
  inputManager.update();
  while (inputManager.isPressed(InputManager::BTN_POWER)) {
    delay(50);
    inputManager.update();
  }
}

// Register only the reader font for the active size (saves ~4.5KB in READER mode)
void setupReaderFontForSize(pixelpaper::Settings::FontSize fontSize) {
  switch (fontSize) {
    case pixelpaper::Settings::FontXSmall:
      renderer.insertFont(READER_FONT_ID_XSMALL, readerFontFamilyXSmall());
      break;
    case pixelpaper::Settings::FontMedium:
      renderer.insertFont(READER_FONT_ID_MEDIUM, readerFontFamilyMedium());
      break;
    case pixelpaper::Settings::FontLarge:
      renderer.insertFont(READER_FONT_ID_LARGE, readerFontFamilyLarge());
      break;
    default:  // FontSmall
      renderer.insertFont(READER_FONT_ID, readerFontFamilySmall());
      break;
  }
}

void setupDisplayAndFonts(bool allReaderSizes = true) {
  if (X3Detect::isX3()) einkDisplay.setDisplayX3();
  einkDisplay.begin();
  renderer.begin();
  LOG_INF(TAG, "Display initialized");
  if (allReaderSizes) {
    renderer.insertFont(READER_FONT_ID_XSMALL, readerFontFamilyXSmall());
    renderer.insertFont(READER_FONT_ID, readerFontFamilySmall());
    renderer.insertFont(READER_FONT_ID_MEDIUM, readerFontFamilyMedium());
    renderer.insertFont(READER_FONT_ID_LARGE, readerFontFamilyLarge());
  } else {
    setupReaderFontForSize(static_cast<pixelpaper::Settings::FontSize>(pixelpaper::core.settings.fontSize));
  }
  renderer.insertFont(UI_FONT_ID, uiFontFamily);
  renderer.insertFont(SMALL_FONT_ID, smallFontFamily);
  renderer.excludeExternalFont(UI_FONT_ID);
  renderer.excludeExternalFont(SMALL_FONT_ID);
  LOG_INF(TAG, "Fonts setup");
}

void applyThemeFonts() {
  Theme& theme = THEME_MANAGER.mutableCurrent();

  // Reset UI font to builtin first in case custom font loading fails
  theme.uiFontId = UI_FONT_ID;

  // Only load the reader font that matches current font size setting
  // This saves ~500KB+ of RAM by not loading all three sizes
  const char* fontFamilyName = nullptr;
  int* targetFontId = nullptr;
  int builtinFontId = 0;
  const bool hasReaderFontOverride = pixelpaper::core.settings.readerFontFamily[0] != '\0';

  switch (pixelpaper::core.settings.fontSize) {
    case pixelpaper::Settings::FontXSmall:
      fontFamilyName = hasReaderFontOverride ? pixelpaper::core.settings.readerFontFamily : theme.readerFontFamilyXSmall;
      targetFontId = &theme.readerFontIdXSmall;
      builtinFontId = READER_FONT_ID_XSMALL;
      break;
    case pixelpaper::Settings::FontMedium:
      fontFamilyName = hasReaderFontOverride ? pixelpaper::core.settings.readerFontFamily : theme.readerFontFamilyMedium;
      targetFontId = &theme.readerFontIdMedium;
      builtinFontId = READER_FONT_ID_MEDIUM;
      break;
    case pixelpaper::Settings::FontLarge:
      fontFamilyName = hasReaderFontOverride ? pixelpaper::core.settings.readerFontFamily : theme.readerFontFamilyLarge;
      targetFontId = &theme.readerFontIdLarge;
      builtinFontId = READER_FONT_ID_LARGE;
      break;
    default:  // FontSmall
      fontFamilyName = hasReaderFontOverride ? pixelpaper::core.settings.readerFontFamily : theme.readerFontFamilySmall;
      targetFontId = &theme.readerFontId;
      builtinFontId = READER_FONT_ID;
      break;
  }

  // Reset to builtin first in case custom font loading fails
  *targetFontId = builtinFontId;

  if (fontFamilyName && fontFamilyName[0] != '\0') {
    int customFontId = FONT_MANAGER.getFontId(fontFamilyName, builtinFontId);
    if (customFontId != builtinFontId) {
      *targetFontId = customFontId;
      LOG_INF(TAG, "Reader font: %s (ID: %d)", fontFamilyName, customFontId);
    }
  }
}

void showErrorScreen(const char* message) {
  renderer.clearScreen(false);
  renderer.drawCenteredText(UI_FONT_ID, 100, message, true, BOLD);
  renderer.displayBuffer();
}

void showSleepSequence() {
  const int w = renderer.getScreenWidth();
  const int h = renderer.getScreenHeight();
  const int titleY = h / 2 - 8;
  const char* title = "PIXELPAPER";

  // Frame 1: white background, black text (normal, crisp)
  renderer.clearScreen(0xFF);
  renderer.drawCenteredText(UI_FONT_ID, titleY, title, true, BOLD);
  renderer.displayBuffer(EInkDisplay::FAST_REFRESH);
  delay(55);

  // Frame 2: black background, white text (numbers creeping in)
  renderer.clearScreen(0x00);
  renderer.drawCenteredText(UI_FONT_ID, titleY, "P!X3L^P@P3R", false, BOLD);
  renderer.displayBuffer(EInkDisplay::FAST_REFRESH);
  delay(65);

  // Frame 3: white background, black text (heavier symbol scramble)
  renderer.clearScreen(0xFF);
  renderer.drawCenteredText(UI_FONT_ID, titleY, "PX|}{L><P4?R", true, BOLD);
  renderer.displayBuffer(EInkDisplay::FAST_REFRESH);
  delay(65);

  // Frame 4: black background, white text (letter/number desync)
  renderer.clearScreen(0x00);
  renderer.drawCenteredText(UI_FONT_ID, titleY, "P?X#L9P%P=R", false, BOLD);
  renderer.displayBuffer(EInkDisplay::FAST_REFRESH);
  delay(65);

  // Frame 5: white background, black text (full noise burst)
  renderer.clearScreen(0xFF);
  renderer.drawCenteredText(UI_FONT_ID, titleY, "@!~#^&*+?/", true, BOLD);
  renderer.displayBuffer(EInkDisplay::FAST_REFRESH);
  delay(65);

  // Frame 6: black screen hold
  renderer.clearScreen(0x00);
  renderer.displayBuffer(EInkDisplay::FAST_REFRESH);
  delay(260);

  // Frame 7: blank / deep sleep (clear screen before sleeping)
  renderer.clearScreen(0xFF);
  renderer.displayBuffer(EInkDisplay::FAST_REFRESH);
}

void showWakeSequence() {
  const int w = renderer.getScreenWidth();
  const int h = renderer.getScreenHeight();
  const int titleY = h / 2 - 8;
  const char* title = "PIXELPAPER";

  // Frame 1: black background, white text (static/noise)
  renderer.clearScreen(0x00);
  renderer.drawCenteredText(UI_FONT_ID, titleY, "@!~#^&*+?/", false, BOLD);
  renderer.displayBuffer(EInkDisplay::FAST_REFRESH);
  delay(55);

  // Frame 2: white background, black text (heavier symbol scramble)
  renderer.clearScreen(0xFF);
  renderer.drawCenteredText(UI_FONT_ID, titleY, "PX|}{L><P4?R", true, BOLD);
  renderer.displayBuffer(EInkDisplay::FAST_REFRESH);
  delay(65);

  // Frame 3: black background, white text (letter/number desync)
  renderer.clearScreen(0x00);
  renderer.drawCenteredText(UI_FONT_ID, titleY, "P?X#L9P%P=R", false, BOLD);
  renderer.displayBuffer(EInkDisplay::FAST_REFRESH);
  delay(65);

  // Frame 4: white background, black text (numbers creeping in)
  renderer.clearScreen(0xFF);
  renderer.drawCenteredText(UI_FONT_ID, titleY, "P!X3L^P@P3R", true, BOLD);
  renderer.displayBuffer(EInkDisplay::FAST_REFRESH);
  delay(65);

  // Frame 5: white background, black text (final stable)
  renderer.clearScreen(0xFF);
  renderer.drawCenteredText(UI_FONT_ID, titleY, title, true, BOLD);
  renderer.displayBuffer(EInkDisplay::FAST_REFRESH);
  delay(85);

  // Frame 6: wait (hold crisp title)
  renderer.displayBuffer(EInkDisplay::FAST_REFRESH);
  delay(240);

  // Frame 7: resume reader (screen stays at current state, app continues)
}

// Track current boot mode for loop behavior
static pixelpaper::BootMode currentBootMode = pixelpaper::BootMode::UI;

// Early initialization - common to both boot modes
// Returns false if critical initialization failed
bool earlyInit() {
  // Only start serial if USB connected
  pinMode(UART0_RXD, INPUT);
  if (isUsbConnected()) {
    Serial.begin(115200);
    delay(SERIAL_INIT_DELAY_MS);  // Allow USB CDC to initialize
    unsigned long start = millis();
    while (!Serial && (millis() - start) < SERIAL_READY_TIMEOUT_MS) {
      delay(SERIAL_INIT_DELAY_MS);
    }
  }

  X3Detect::detect();
  LOG_INF(TAG, "Hardware: %s", X3Detect::isX3() ? "X3" : "X4");

  inputManager.begin();

  // Initialize SPI and SD card before wakeup verification so settings are available
  SPI.begin(EPD_SCLK, SD_SPI_MISO, EPD_MOSI, EPD_CS);
  if (!SdMan.begin()) {
    LOG_ERR(TAG, "SD card initialization failed");
    setupDisplayAndFonts();
    showErrorScreen("SD card error");
    return false;
  }

  // Load settings before wakeup verification - without this, a full power cycle
  // (no USB) resets RTC memory and the short power button setting is ignored
  pixelpaper::core.settings.loadFromFile();
  rtcPowerButtonDurationMs = pixelpaper::core.settings.getPowerButtonDuration();

  const auto wakeup = getWakeupInfo();
  if (wakeup.isPowerButton) {
    verifyWakeupLongPress(wakeup.resetReason);
  }

  LOG_INF(TAG, "Starting Pixelpaper version " PIXELPAPER_VERSION);

  // X4: battery via ADC. X3: battery via BQ27220 I2C fuel gauge (no ADC pin needed)
  if (X3Detect::isX4()) analogSetPinAttenuation(BAT_GPIO0, ADC_11db);

  // Initialize internal flash filesystem for font storage
  if (!LittleFS.begin(false)) {
    LOG_ERR(TAG, "LittleFS mount failed, attempting format");
    if (!LittleFS.format() || !LittleFS.begin(false)) {
      LOG_ERR(TAG, "LittleFS recovery failed");
      showErrorScreen("Internal storage error");
      return false;
    }
    LOG_INF(TAG, "LittleFS formatted and mounted");
  } else {
    LOG_INF(TAG, "LittleFS mounted");
  }

  return true;
}

// Initialize UI mode - full state registration, all resources
void initUIMode() {
  LOG_INF(TAG, "Initializing UI mode");
  LOG_DBG(TAG, "[UI mode] Free heap: %lu, Max block: %lu", ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  // Initialize theme and font managers (full)
  FONT_MANAGER.init(renderer);
  THEME_MANAGER.loadTheme(pixelpaper::core.settings.themeName);
  THEME_MANAGER.createDefaultThemeFiles();
  LOG_INF(TAG, "Theme loaded: %s", THEME_MANAGER.currentThemeName());

  setupDisplayAndFonts();
  applyThemeFonts();

  const auto& preInitTransition = pixelpaper::getTransition();
  if (preInitTransition.isValid()) {
    pixelpaper::showTransitionLoadingAnimation(1, 170);
  }

  // Register ALL states for UI mode
  stateMachine.registerState(&startupState);
  stateMachine.registerState(&fileListState);
  stateMachine.registerState(&readerState);
  stateMachine.registerState(&settingsState);
  stateMachine.registerState(&networkState);
  stateMachine.registerState(&calibreSyncState);
  stateMachine.registerState(&appLauncherState);
  stateMachine.registerState(&sleepState);
  stateMachine.registerState(&errorState);

  // Initialize core
  auto result = pixelpaper::core.init();
  if (!result.ok()) {
    LOG_ERR(TAG, "Init failed: %s", pixelpaper::errorToString(result.err));
    showErrorScreen("Core init failed");
    return;
  }

  // Enforce physical front button order: B1=Back, B2=Menu, B3=Prev, B4=Next.
  if (pixelpaper::core.settings.frontButtonLayout != pixelpaper::Settings::FrontBCLR) {
    pixelpaper::core.settings.frontButtonLayout = pixelpaper::Settings::FrontBCLR;
    pixelpaper::core.settings.save(pixelpaper::core.storage);
  }

  LOG_INF(TAG, "State machine starting (UI mode)");
  mappedInputManager.setSettings(&pixelpaper::core.settings);
  ui::setFrontButtonLayout(pixelpaper::core.settings.frontButtonLayout);

  // Determine initial state - check for return from reader mode
  pixelpaper::StateId initialState = pixelpaper::StateId::FileList;
  const auto& transition = pixelpaper::getTransition();

  if (transition.returnTo == pixelpaper::ReturnTo::SETTINGS) {
    initialState = pixelpaper::StateId::Settings;
    LOG_INF(TAG, "Returning to Settings from Reader");
  } else if (transition.returnTo == pixelpaper::ReturnTo::FILE_MANAGER) {
    initialState = pixelpaper::StateId::FileList;
    LOG_INF(TAG, "Returning to FileList from Reader");
  } else {
    LOG_INF(TAG, "Starting at FileList");
  }

  stateMachine.init(pixelpaper::core, initialState);

  // Force initial render
  LOG_DBG(TAG, "Forcing initial render");
  stateMachine.update(pixelpaper::core);

  LOG_DBG(TAG, "[UI mode] After init - Free heap: %lu, Max block: %lu", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
}

// Initialize Reader mode - minimal state registration with all reader font sizes
void initReaderMode() {
  LOG_INF(TAG, "Initializing READER mode");
  LOG_DBG(TAG, "[READER mode] Free heap: %lu, Max block: %lu", ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  // Detect content type early to decide if we need custom fonts
  // XTC/XTCH files contain pre-rendered bitmaps and don't need fonts for page rendering
  const auto& transition = pixelpaper::getTransition();
  pixelpaper::ContentType contentType = pixelpaper::detectContentType(transition.bookPath);
  bool needsCustomFonts = (contentType != pixelpaper::ContentType::Xtc);

  // Initialize theme and font managers (minimal - no cache)
  FONT_MANAGER.init(renderer);
  THEME_MANAGER.loadTheme(pixelpaper::core.settings.themeName);
  // Skip createDefaultThemeFiles() - not needed in reader mode
  LOG_INF(TAG, "Theme loaded: %s (reader mode)", THEME_MANAGER.currentThemeName());

  // Reader can now open Settings in-process and change font size without a reboot.
  // Load all reader sizes here so returning to Reader never references an
  // unloaded font ID.
  setupDisplayAndFonts(true);

  if (needsCustomFonts) {
    applyThemeFonts();  // Custom fonts - skip for XTC/XTCH to save ~500KB+ RAM
  } else {
    LOG_DBG(TAG, "Skipping custom fonts for XTC content");
  }

  // Register reader plus lightweight UI states so reader->library/settings can
  // switch in-process without reboot/loading overlays.
  stateMachine.registerState(&readerState);
  stateMachine.registerState(&fileListState);
  stateMachine.registerState(&settingsState);
  stateMachine.registerState(&networkState);
  stateMachine.registerState(&sleepState);
  stateMachine.registerState(&errorState);

  // Initialize core
  auto result = pixelpaper::core.init();
  if (!result.ok()) {
    LOG_ERR(TAG, "Init failed: %s", pixelpaper::errorToString(result.err));
    showErrorScreen("Core init failed");
    return;
  }

  // Enforce physical front button order in reader mode too.
  if (pixelpaper::core.settings.frontButtonLayout != pixelpaper::Settings::FrontBCLR) {
    pixelpaper::core.settings.frontButtonLayout = pixelpaper::Settings::FrontBCLR;
    pixelpaper::core.settings.save(pixelpaper::core.storage);
  }

  LOG_INF(TAG, "State machine starting (READER mode)");
  mappedInputManager.setSettings(&pixelpaper::core.settings);
  ui::setFrontButtonLayout(pixelpaper::core.settings.frontButtonLayout);

  if (transition.bookPath[0] != '\0') {
    // Copy path to shared buffer for ReaderState to consume
    strncpy(pixelpaper::core.buf.path, transition.bookPath, sizeof(pixelpaper::core.buf.path) - 1);
    pixelpaper::core.buf.path[sizeof(pixelpaper::core.buf.path) - 1] = '\0';
    LOG_INF(TAG, "Opening book: %s", pixelpaper::core.buf.path);
  } else {
    // No book path - fall back to UI mode to avoid boot loop
    LOG_ERR(TAG, "No book path in transition, falling back to UI");
    initUIMode();
    return;
  }

  stateMachine.init(pixelpaper::core, pixelpaper::StateId::Reader);

  // Force initial render
  LOG_DBG(TAG, "Forcing initial render");
  stateMachine.update(pixelpaper::core);

  LOG_DBG(TAG, "[READER mode] After init - Free heap: %lu, Max block: %lu", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
}

void setup() {
  // Early initialization (common to both modes)
  if (!earlyInit()) {
    return;  // Critical failure
  }

  // Check if waking from deep sleep via power button
  const auto wakeup = getWakeupInfo();
  const bool wakeFromSleep = wakeup.isPowerButton && 
    (wakeup.resetReason == ESP_RST_DEEPSLEEP || wakeup.resetReason == ESP_RST_POWERON);

  // Show wake sequence if recovering from sleep
  if (wakeFromSleep) {
    setupDisplayAndFonts();
    showWakeSequence();
  }

  // Detect boot mode from RTC memory or settings
  currentBootMode = pixelpaper::detectBootMode();

  if (currentBootMode == pixelpaper::BootMode::READER) {
    initReaderMode();
  } else {
    initUIMode();
  }

  // Ensure we're not still holding the power button before leaving setup
  waitForPowerRelease();
}

void loop() {
  static unsigned long maxLoopDuration = 0;
  const unsigned long loopStartTime = millis();
  static unsigned long lastMemPrint = 0;

  inputManager.update();

  if (!pixelpaper::core.cpu.isThrottled() && millis() - lastMemPrint >= 10000) {
    LOG_DBG(TAG, "Free: %d bytes, Total: %d bytes, Min Free: %d bytes, MaxAlloc: %d bytes", ESP.getFreeHeap(),
            ESP.getHeapSize(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());
    lastMemPrint = millis();
  }

  // Poll input and push events to queue
  pixelpaper::core.input.poll();

  // Auto-sleep after inactivity
  const auto autoSleepTimeout = pixelpaper::core.settings.getAutoSleepTimeoutMs();
  const bool wifiActive = pixelpaper::core.network.isConnected() || pixelpaper::core.network.isAPMode();
  if (wifiActive) {
    pixelpaper::core.input.resetIdleTimer();
  }
  if (autoSleepTimeout > 0 && pixelpaper::core.input.idleTimeMs() >= autoSleepTimeout) {
    LOG_INF(TAG, "Auto-sleep after %lu ms idle", autoSleepTimeout);
    stateMachine.init(pixelpaper::core, pixelpaper::StateId::Sleep);
    return;
  }

  // Power button sleep check: track held time that excludes long rendering gaps
  // where button state changes could have been missed by inputManager
  {
    static unsigned long powerHeldSinceMs = 0;
    static unsigned long prevPowerCheckMs = 0;
    const unsigned long loopGap = loopStartTime - prevPowerCheckMs;
    prevPowerCheckMs = loopStartTime;

    if (inputManager.isPressed(InputManager::BTN_POWER)) {
      if (powerHeldSinceMs == 0 || loopGap > 100) {
        powerHeldSinceMs = loopStartTime;
      }
      if (loopStartTime - powerHeldSinceMs > pixelpaper::core.settings.getPowerButtonDuration()) {
        stateMachine.init(pixelpaper::core, pixelpaper::StateId::Sleep);
        return;
      }
    } else {
      powerHeldSinceMs = 0;
    }
  }

  // CPU frequency scaling: drop to 10 MHz after idle to save battery,
  // restore full speed on any activity. Must run BEFORE stateMachine.update()
  // so rendering always happens at full CPU/SPI speed after wake.
  // Idea: CrossPoint HalPowerManager by @ngxson (https://github.com/ngxson)
  static constexpr unsigned long kIdlePowerSavingMs = 3000;
  if (currentBootMode == pixelpaper::BootMode::READER) {
    if (pixelpaper::core.input.idleTimeMs() >= kIdlePowerSavingMs) {
      pixelpaper::core.cpu.throttle();
    } else {
      pixelpaper::core.cpu.unthrottle();
    }
  }

  // Update state machine (handles transitions and rendering)
  const unsigned long activityStartTime = millis();
  stateMachine.update(pixelpaper::core);
  const unsigned long activityDuration = millis() - activityStartTime;

  const unsigned long loopDuration = millis() - loopStartTime;
  if (loopDuration > maxLoopDuration) {
    maxLoopDuration = loopDuration;
    if (maxLoopDuration > 50) {
      LOG_DBG(TAG, "New max loop duration: %lu ms (activity: %lu ms)", maxLoopDuration, activityDuration);
    }
  }

  // Add delay at the end of the loop to prevent tight spinning
  // Increase delay after idle to save power (~4x less CPU load)
  // Idea: https://github.com/crosspoint-reader/crosspoint-reader/commit/0991782 by @ngxson (https://github.com/ngxson)
  delay(pixelpaper::core.cpu.loopDelayMs());
}
