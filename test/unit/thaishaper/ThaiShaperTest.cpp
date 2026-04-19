// ThaiShaper library unit tests
// Tests character classification, cluster building, word segmentation,
// and cluster boundary detection for Thai text rendering.

#include "test_utils.h"

#include <ThaiShaper.h>

#include <string>
#include <vector>

using namespace ThaiShaper;

// Helper to compare ThaiCharType (enum class can't be streamed by expectEq)
#define EXPECT_CHAR_TYPE(expected, actual, name) \
  runner.expectTrue((expected) == (actual), name)

int main() {
  TestUtils::TestRunner runner("ThaiShaper");

  // ============================================
  // 1. ThaiCharacter classification: getThaiCharType()
  // ============================================

  // Consonants
  EXPECT_CHAR_TYPE(ThaiCharType::CONSONANT, getThaiCharType(0x0E01), "getThaiCharType: U+0E01 (KO KAI) -> CONSONANT");
  EXPECT_CHAR_TYPE(ThaiCharType::CONSONANT, getThaiCharType(0x0E2E), "getThaiCharType: U+0E2E (HO NOKHUK) -> CONSONANT");

  // Leading vowel
  EXPECT_CHAR_TYPE(ThaiCharType::LEADING_VOWEL, getThaiCharType(0x0E40),
                   "getThaiCharType: U+0E40 (SARA E) -> LEADING_VOWEL");

  // Above vowels
  EXPECT_CHAR_TYPE(ThaiCharType::ABOVE_VOWEL, getThaiCharType(0x0E34),
                   "getThaiCharType: U+0E34 (SARA I) -> ABOVE_VOWEL");
  EXPECT_CHAR_TYPE(ThaiCharType::ABOVE_VOWEL, getThaiCharType(0x0E47),
                   "getThaiCharType: U+0E47 (MAITAIKHU) -> ABOVE_VOWEL");

  // Below vowel
  EXPECT_CHAR_TYPE(ThaiCharType::BELOW_VOWEL, getThaiCharType(0x0E38),
                   "getThaiCharType: U+0E38 (SARA U) -> BELOW_VOWEL");

  // Follow vowel
  EXPECT_CHAR_TYPE(ThaiCharType::FOLLOW_VOWEL, getThaiCharType(0x0E32),
                   "getThaiCharType: U+0E32 (SARA AA) -> FOLLOW_VOWEL");

  // Tone mark
  EXPECT_CHAR_TYPE(ThaiCharType::TONE_MARK, getThaiCharType(0x0E48),
                   "getThaiCharType: U+0E48 (MAI EK) -> TONE_MARK");

  // Nikhahit
  EXPECT_CHAR_TYPE(ThaiCharType::NIKHAHIT, getThaiCharType(0x0E4D), "getThaiCharType: U+0E4D (NIKHAHIT) -> NIKHAHIT");

  // Yamakkan (Thanthakhat classified as YAMAKKAN)
  EXPECT_CHAR_TYPE(ThaiCharType::YAMAKKAN, getThaiCharType(0x0E4C),
                   "getThaiCharType: U+0E4C (THANTHAKHAT) -> YAMAKKAN");

  // Thai digit
  EXPECT_CHAR_TYPE(ThaiCharType::THAI_DIGIT, getThaiCharType(0x0E50),
                   "getThaiCharType: U+0E50 (THAI ZERO) -> THAI_DIGIT");

  // Thai symbol
  EXPECT_CHAR_TYPE(ThaiCharType::THAI_SYMBOL, getThaiCharType(0x0E2F),
                   "getThaiCharType: U+0E2F (PAIYANNOI) -> THAI_SYMBOL");

  // Non-Thai
  EXPECT_CHAR_TYPE(ThaiCharType::NON_THAI, getThaiCharType(0x41), "getThaiCharType: 'A' (0x41) -> NON_THAI");
  EXPECT_CHAR_TYPE(ThaiCharType::NON_THAI, getThaiCharType(0x0E80), "getThaiCharType: U+0E80 -> NON_THAI");
  EXPECT_CHAR_TYPE(ThaiCharType::NON_THAI, getThaiCharType(0x0DFF), "getThaiCharType: U+0DFF -> NON_THAI");

  // ============================================
  // 2. Boundary codepoints
  // ============================================

  EXPECT_CHAR_TYPE(ThaiCharType::CONSONANT, getThaiCharType(0x0E01),
                   "boundary: first Thai block U+0E01 -> CONSONANT");
  EXPECT_CHAR_TYPE(ThaiCharType::THAI_SYMBOL, getThaiCharType(0x0E7F),
                   "boundary: last Thai block U+0E7F -> THAI_SYMBOL");
  EXPECT_CHAR_TYPE(ThaiCharType::NON_THAI, getThaiCharType(0x0DFF), "boundary: just before Thai U+0DFF -> NON_THAI");
  EXPECT_CHAR_TYPE(ThaiCharType::NON_THAI, getThaiCharType(0x0E80), "boundary: just after Thai U+0E80 -> NON_THAI");

  // ============================================
  // 3. isThaiCombining()
  // ============================================

  runner.expectTrue(isThaiCombining(0x0E34), "isThaiCombining: above vowel U+0E34 -> true");
  runner.expectTrue(isThaiCombining(0x0E38), "isThaiCombining: below vowel U+0E38 -> true");
  runner.expectTrue(isThaiCombining(0x0E48), "isThaiCombining: tone mark U+0E48 -> true");
  runner.expectTrue(isThaiCombining(0x0E4C), "isThaiCombining: thanthakhat U+0E4C -> true");
  runner.expectTrue(isThaiCombining(0x0E4D), "isThaiCombining: nikhahit U+0E4D -> true");
  runner.expectTrue(isThaiCombining(0x0E4E), "isThaiCombining: yamakkan U+0E4E -> true");
  runner.expectFalse(isThaiCombining(0x0E01), "isThaiCombining: consonant U+0E01 -> false");
  runner.expectFalse(isThaiCombining(0x0E50), "isThaiCombining: digit U+0E50 -> false");
  runner.expectFalse(isThaiCombining(0x41), "isThaiCombining: 'A' -> false");

  // ============================================
  // 4. containsThai()
  // ============================================

  runner.expectFalse(containsThai(nullptr), "containsThai: nullptr -> false");
  runner.expectFalse(containsThai(""), "containsThai: empty string -> false");
  runner.expectFalse(containsThai("Hello"), "containsThai: pure ASCII -> false");
  runner.expectTrue(containsThai("\xE0\xB8\x81"), "containsThai: single Thai consonant -> true");
  runner.expectTrue(containsThai("Hello \xE0\xB8\x81 World"), "containsThai: mixed Thai + ASCII -> true");
  runner.expectFalse(containsThai("\xD8\xA8"), "containsThai: Arabic Beh -> false");

  // ============================================
  // 5. ThaiClusterBuilder buildClusters()
  // ============================================

  // nullptr and empty
  {
    auto clusters = ThaiClusterBuilder::buildClusters(nullptr);
    runner.expectEq(size_t(0), clusters.size(), "buildClusters: nullptr -> empty");
  }
  {
    auto clusters = ThaiClusterBuilder::buildClusters("");
    runner.expectEq(size_t(0), clusters.size(), "buildClusters: empty string -> empty");
  }

  // Single consonant
  {
    auto clusters = ThaiClusterBuilder::buildClusters("\xE0\xB8\x81");  // ก
    runner.expectEq(size_t(1), clusters.size(), "buildClusters: single consonant -> 1 cluster");
    if (clusters.size() == 1) {
      runner.expectEq(size_t(1), clusters[0].glyphs.size(), "buildClusters: single consonant -> 1 glyph");
      runner.expectEq(uint32_t(0x0E01), clusters[0].glyphs[0].codepoint, "buildClusters: consonant codepoint");
      runner.expectEq(int8_t(0), clusters[0].glyphs[0].xOffset, "buildClusters: consonant xOffset=0");
      runner.expectEq(int8_t(0), clusters[0].glyphs[0].yOffset, "buildClusters: consonant yOffset=0");
      runner.expectFalse(clusters[0].glyphs[0].zeroAdvance, "buildClusters: consonant zeroAdvance=false");
    }
  }

  // Consonant + above vowel: กิ
  {
    auto clusters = ThaiClusterBuilder::buildClusters("\xE0\xB8\x81\xE0\xB8\xB4");  // ก + ิ
    runner.expectEq(size_t(1), clusters.size(), "buildClusters: consonant+above vowel -> 1 cluster");
    if (clusters.size() == 1) {
      runner.expectEq(size_t(2), clusters[0].glyphs.size(), "buildClusters: consonant+above -> 2 glyphs");
      runner.expectEq(uint32_t(0x0E01), clusters[0].glyphs[0].codepoint, "buildClusters: base is consonant");
      runner.expectFalse(clusters[0].glyphs[0].zeroAdvance, "buildClusters: consonant advances");
      runner.expectEq(uint32_t(0x0E34), clusters[0].glyphs[1].codepoint, "buildClusters: second is above vowel");
      runner.expectEq(ThaiOffset::ABOVE_VOWEL, clusters[0].glyphs[1].yOffset,
                      "buildClusters: above vowel yOffset == ABOVE_VOWEL");
      runner.expectTrue(clusters[0].glyphs[1].zeroAdvance, "buildClusters: above vowel zeroAdvance=true");
    }
  }

  // Consonant + above vowel + tone mark: กี่
  {
    auto clusters =
        ThaiClusterBuilder::buildClusters("\xE0\xB8\x81\xE0\xB8\xB5\xE0\xB9\x88");  // ก + ี + ่
    runner.expectEq(size_t(1), clusters.size(), "buildClusters: consonant+above+tone -> 1 cluster");
    if (clusters.size() == 1) {
      runner.expectEq(size_t(3), clusters[0].glyphs.size(), "buildClusters: consonant+above+tone -> 3 glyphs");
      // Tone mark should be above the above vowel (more negative)
      runner.expectEq(ThaiOffset::TONE_MARK, clusters[0].glyphs[2].yOffset,
                      "buildClusters: tone mark yOffset == TONE_MARK (above vowel present)");
      runner.expectTrue(clusters[0].glyphs[2].yOffset < clusters[0].glyphs[1].yOffset,
                        "buildClusters: tone mark y < above vowel y (both negative, tone higher)");
    }
  }

  // Leading vowel + consonant: เก
  {
    auto clusters = ThaiClusterBuilder::buildClusters("\xE0\xB9\x80\xE0\xB8\x81");  // เ + ก
    runner.expectEq(size_t(1), clusters.size(), "buildClusters: leading vowel+consonant -> 1 cluster");
    if (clusters.size() == 1) {
      runner.expectEq(size_t(2), clusters[0].glyphs.size(), "buildClusters: leading+consonant -> 2 glyphs");
      runner.expectEq(uint32_t(0x0E40), clusters[0].glyphs[0].codepoint,
                      "buildClusters: leading vowel rendered first");
      runner.expectEq(uint32_t(0x0E01), clusters[0].glyphs[1].codepoint,
                      "buildClusters: consonant rendered second");
    }
  }

  // Consonant + below vowel: กุ
  {
    auto clusters = ThaiClusterBuilder::buildClusters("\xE0\xB8\x81\xE0\xB8\xB8");  // ก + ุ
    runner.expectEq(size_t(1), clusters.size(), "buildClusters: consonant+below vowel -> 1 cluster");
    if (clusters.size() == 1) {
      runner.expectEq(size_t(2), clusters[0].glyphs.size(), "buildClusters: consonant+below -> 2 glyphs");
      runner.expectEq(ThaiOffset::BELOW_VOWEL, clusters[0].glyphs[1].yOffset,
                      "buildClusters: below vowel yOffset == BELOW_VOWEL");
      runner.expectTrue(clusters[0].glyphs[1].yOffset > 0, "buildClusters: below vowel yOffset positive");
      runner.expectTrue(clusters[0].glyphs[1].zeroAdvance, "buildClusters: below vowel zeroAdvance=true");
    }
  }

  // Ascender consonant + above vowel + tone mark: ปิ่
  {
    auto clusters =
        ThaiClusterBuilder::buildClusters("\xE0\xB8\x9B\xE0\xB8\xB4\xE0\xB9\x88");  // ป + ิ + ่
    runner.expectEq(size_t(1), clusters.size(), "buildClusters: ascender+above+tone -> 1 cluster");
    if (clusters.size() == 1) {
      runner.expectEq(size_t(3), clusters[0].glyphs.size(), "buildClusters: ascender+above+tone -> 3 glyphs");
      runner.expectEq(ThaiOffset::ASCENDER_X_SHIFT, clusters[0].glyphs[1].xOffset,
                      "buildClusters: above vowel xOffset == ASCENDER_X_SHIFT for ascender consonant");
      runner.expectEq(ThaiOffset::ASCENDER_X_SHIFT, clusters[0].glyphs[2].xOffset,
                      "buildClusters: tone mark xOffset == ASCENDER_X_SHIFT for ascender consonant");
    }
  }

  // Non-Thai character
  {
    auto clusters = ThaiClusterBuilder::buildClusters("A");
    runner.expectEq(size_t(1), clusters.size(), "buildClusters: non-Thai 'A' -> 1 cluster");
    if (clusters.size() == 1) {
      runner.expectEq(size_t(1), clusters[0].glyphs.size(), "buildClusters: non-Thai -> 1 glyph");
      runner.expectEq(uint32_t('A'), clusters[0].glyphs[0].codepoint, "buildClusters: non-Thai codepoint='A'");
      runner.expectFalse(clusters[0].glyphs[0].zeroAdvance, "buildClusters: non-Thai zeroAdvance=false");
    }
  }

  // Mixed: A + ก
  {
    auto clusters = ThaiClusterBuilder::buildClusters("A\xE0\xB8\x81");
    runner.expectEq(size_t(2), clusters.size(), "buildClusters: mixed 'A'+Thai -> 2 clusters");
  }

  // ============================================
  // 6. ThaiWordBreak segmentWords()
  // ============================================

  // nullptr and empty
  {
    auto segments = ThaiWordBreak::segmentWords(nullptr);
    runner.expectEq(size_t(0), segments.size(), "segmentWords: nullptr -> empty");
  }
  {
    auto segments = ThaiWordBreak::segmentWords("");
    runner.expectEq(size_t(0), segments.size(), "segmentWords: empty string -> empty");
  }

  // Single Thai cluster: กา (consonant + follow vowel)
  {
    auto segments = ThaiWordBreak::segmentWords("\xE0\xB8\x81\xE0\xB8\xB2");  // กา
    runner.expectEq(size_t(1), segments.size(), "segmentWords: single cluster -> 1 segment");
    if (segments.size() == 1) {
      runner.expectEqual("\xE0\xB8\x81\xE0\xB8\xB2", segments[0], "segmentWords: cluster content matches");
    }
  }

  // Whitespace produces separate segments: "ก า"
  {
    auto segments = ThaiWordBreak::segmentWords("\xE0\xB8\x81 \xE0\xB8\xB2");  // ก space า
    runner.expectEq(size_t(3), segments.size(), "segmentWords: whitespace -> 3 segments");
    if (segments.size() == 3) {
      runner.expectEqual("\xE0\xB8\x81", segments[0], "segmentWords: first segment is consonant");
      runner.expectEqual(" ", segments[1], "segmentWords: second segment is space");
      runner.expectEqual("\xE0\xB8\xB2", segments[2], "segmentWords: third segment is vowel");
    }
  }

  // Large text (exceeding MAX_SEGMENT_TEXT_SIZE=512) does not crash
  {
    // Each Thai consonant ก is 3 bytes; 200 repetitions = 600 bytes > 512
    std::string longText;
    for (int i = 0; i < 200; i++) {
      longText += "\xE0\xB8\x81";
    }
    auto segments = ThaiWordBreak::segmentWords(longText.c_str());
    runner.expectTrue(!segments.empty(), "segmentWords: long text (600 bytes) -> non-empty result");
  }

  // ============================================
  // 7. nextClusterBoundary()
  // ============================================

  // nullptr
  runner.expectEq(size_t(0), ThaiWordBreak::nextClusterBoundary(nullptr, 0),
                  "nextClusterBoundary: nullptr -> 0");

  // Leading vowel + consonant: เก (6 bytes total)
  {
    const char* text = "\xE0\xB9\x80\xE0\xB8\x81";  // เก
    size_t boundary = ThaiWordBreak::nextClusterBoundary(text, 0);
    runner.expectEq(size_t(6), boundary, "nextClusterBoundary: leading vowel+consonant -> 6");
  }

  // Follow vowel terminates cluster: กา (6 bytes total)
  {
    const char* text = "\xE0\xB8\x81\xE0\xB8\xB2";  // กา
    size_t boundary = ThaiWordBreak::nextClusterBoundary(text, 0);
    runner.expectEq(size_t(6), boundary, "nextClusterBoundary: consonant+follow vowel -> 6");
  }

  // Non-Thai byte 'A' at offset 0
  {
    const char* text = "A";
    size_t boundary = ThaiWordBreak::nextClusterBoundary(text, 0);
    runner.expectEq(size_t(1), boundary, "nextClusterBoundary: non-Thai 'A' -> 1");
  }

  return runner.allPassed() ? 0 : 1;
}
