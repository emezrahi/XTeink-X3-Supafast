#include "test_utils.h"

#include <cstdint>

#include "ScriptDetector.h"

int main() {
  TestUtils::TestRunner runner("ScriptDetector Extended");

  // ============================================
  // containsThai() tests
  // ============================================

  runner.expectFalse(ScriptDetector::containsThai(nullptr), "containsThai: nullptr");
  runner.expectFalse(ScriptDetector::containsThai(""), "containsThai: empty string");
  runner.expectFalse(ScriptDetector::containsThai("Hello World"), "containsThai: pure ASCII");

  // Thai consonant Ko Kai U+0E01
  runner.expectTrue(ScriptDetector::containsThai("\xE0\xB8\x81"), "containsThai: single Thai consonant");

  // Thai digit zero U+0E50
  runner.expectTrue(ScriptDetector::containsThai("\xE0\xB9\x90"), "containsThai: Thai digit");

  // Mixed Latin + Thai
  runner.expectTrue(ScriptDetector::containsThai("Hello \xE0\xB8\x81 World"),
                    "containsThai: mixed Latin+Thai");

  // Pure Arabic (no Thai)
  runner.expectFalse(ScriptDetector::containsThai("\xD8\xA8"), "containsThai: pure Arabic");

  // Just before Thai block U+0DFF
  runner.expectFalse(ScriptDetector::containsThai("\xE0\xB7\xBF"),
                     "containsThai: U+0DFF just before Thai block");

  // Just after Thai block U+0E80
  runner.expectFalse(ScriptDetector::containsThai("\xE0\xBA\x80"),
                     "containsThai: U+0E80 just after Thai block");

  // ============================================
  // containsCjk() tests
  // ============================================

  runner.expectFalse(ScriptDetector::containsCjk(nullptr), "containsCjk: nullptr");
  runner.expectFalse(ScriptDetector::containsCjk(""), "containsCjk: empty string");
  runner.expectFalse(ScriptDetector::containsCjk("Hello"), "containsCjk: pure ASCII");

  // CJK Unified Ideograph U+4E2D
  runner.expectTrue(ScriptDetector::containsCjk("\xE4\xB8\xAD"), "containsCjk: CJK Unified");

  // Hiragana U+3041
  runner.expectTrue(ScriptDetector::containsCjk("\xE3\x81\x81"), "containsCjk: Hiragana");

  // Katakana U+30A2
  runner.expectTrue(ScriptDetector::containsCjk("\xE3\x82\xA2"), "containsCjk: Katakana");

  // Hangul U+AC00
  runner.expectTrue(ScriptDetector::containsCjk("\xEA\xB0\x80"), "containsCjk: Hangul");

  // Fullwidth A U+FF21
  runner.expectTrue(ScriptDetector::containsCjk("\xEF\xBC\xA1"), "containsCjk: Fullwidth");

  // Mixed text with one CJK char
  runner.expectTrue(ScriptDetector::containsCjk("Hello \xE4\xB8\xAD World"),
                    "containsCjk: mixed Latin+CJK");

  // Pure Thai (no CJK)
  runner.expectFalse(ScriptDetector::containsCjk("\xE0\xB8\x81"), "containsCjk: pure Thai");

  // Pure Arabic (no CJK)
  runner.expectFalse(ScriptDetector::containsCjk("\xD8\xA8"), "containsCjk: pure Arabic");

  // CJK Extension B U+20000
  runner.expectTrue(ScriptDetector::containsCjk("\xF0\xA0\x80\x80"),
                    "containsCjk: CJK Extension B");

  // ============================================
  // classify() edge cases
  // ============================================

  // Combining grave accent U+0300 - not in any recognized range, returns OTHER
  runner.expectTrue(ScriptDetector::classify("\xCC\x80") == ScriptDetector::Script::OTHER,
                    "classify: combining mark U+0300 = OTHER");

  // Emoji U+1F600 - not in any recognized range, returns OTHER
  runner.expectTrue(ScriptDetector::classify("\xF0\x9F\x98\x80") == ScriptDetector::Script::OTHER,
                    "classify: emoji U+1F600 = OTHER");

  // CJK Extension B U+20000 - isCjkCodepoint returns true
  runner.expectTrue(
      ScriptDetector::classify("\xF0\xA0\x80\x80") == ScriptDetector::Script::CJK,
      "classify: CJK Extension B U+20000 = CJK");

  // Pure Cyrillic word (U+041F U+0440 U+0438 = "Pri") - Cyrillic range 0x0400-0x04FF -> LATIN
  runner.expectTrue(
      ScriptDetector::classify("\xD0\x9F\xD1\x80\xD0\xB8") == ScriptDetector::Script::LATIN,
      "classify: Cyrillic word = LATIN");

  return runner.allPassed() ? 0 : 1;
}
