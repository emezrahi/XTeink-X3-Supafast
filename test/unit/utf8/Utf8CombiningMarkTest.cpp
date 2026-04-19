#include "test_utils.h"

#include <Utf8.h>
#include <cstdint>

int main() {
  TestUtils::TestRunner runner("utf8IsCombiningMark");

  // ============================================
  // Combining Diacritical Marks (0x0300-0x036F)
  // ============================================

  runner.expectTrue(utf8IsCombiningMark(0x0300),
                    "U+0300 (combining grave accent) is combining mark");
  runner.expectTrue(utf8IsCombiningMark(0x036F),
                    "U+036F (last in Combining Diacritical Marks) is combining mark");
  runner.expectFalse(utf8IsCombiningMark(0x02FF),
                     "U+02FF (just before range) is not combining mark");
  runner.expectFalse(utf8IsCombiningMark(0x0370),
                     "U+0370 (just after range) is not combining mark");

  // ============================================
  // Combining Diacritical Marks Extended (0x1DC0-0x1DFF)
  // ============================================

  runner.expectTrue(utf8IsCombiningMark(0x1DC0),
                    "U+1DC0 (first in Supplement) is combining mark");
  runner.expectTrue(utf8IsCombiningMark(0x1DFF),
                    "U+1DFF (last in Supplement) is combining mark");
  runner.expectFalse(utf8IsCombiningMark(0x1DBF),
                     "U+1DBF (just before Supplement) is not combining mark");
  runner.expectFalse(utf8IsCombiningMark(0x1E00),
                     "U+1E00 (just after Supplement) is not combining mark");

  // ============================================
  // Combining Diacritical Marks for Symbols (0x20D0-0x20FF)
  // ============================================

  runner.expectTrue(utf8IsCombiningMark(0x20D0),
                    "U+20D0 (first in For Symbols) is combining mark");
  runner.expectTrue(utf8IsCombiningMark(0x20FF),
                    "U+20FF (last in For Symbols) is combining mark");
  runner.expectFalse(utf8IsCombiningMark(0x20CF),
                     "U+20CF (just before For Symbols) is not combining mark");
  runner.expectFalse(utf8IsCombiningMark(0x2100),
                     "U+2100 (just after For Symbols) is not combining mark");

  // ============================================
  // Combining Half Marks (0xFE20-0xFE2F)
  // ============================================

  runner.expectTrue(utf8IsCombiningMark(0xFE20),
                    "U+FE20 (first in Half Marks) is combining mark");
  runner.expectTrue(utf8IsCombiningMark(0xFE2F),
                    "U+FE2F (last in Half Marks) is combining mark");
  runner.expectFalse(utf8IsCombiningMark(0xFE1F),
                     "U+FE1F (just before Half Marks) is not combining mark");
  runner.expectFalse(utf8IsCombiningMark(0xFE30),
                     "U+FE30 (just after Half Marks) is not combining mark");

  // ============================================
  // Non-combining characters
  // ============================================

  runner.expectFalse(utf8IsCombiningMark(0x61),
                     "U+0061 (ASCII 'a') is not combining mark");
  runner.expectFalse(utf8IsCombiningMark(0x20),
                     "U+0020 (space) is not combining mark");
  runner.expectFalse(utf8IsCombiningMark(0x0600),
                     "U+0600 (Arabic number sign) is not combining mark");

  // ============================================
  // Thai combining marks (outside Unicode combining mark ranges)
  // ============================================

  runner.expectFalse(utf8IsCombiningMark(0x0E31),
                     "U+0E31 (Thai Mai Han-Akat) is not in combining mark ranges");
  runner.expectFalse(utf8IsCombiningMark(0x0E34),
                     "U+0E34 (Thai Sara I) is not in combining mark ranges");

  // ============================================
  // Arabic diacritics (outside Unicode combining mark ranges)
  // ============================================

  runner.expectFalse(utf8IsCombiningMark(0x064B),
                     "U+064B (Arabic Fathatan) is not in combining mark ranges");

  return runner.allPassed() ? 0 : 1;
}
