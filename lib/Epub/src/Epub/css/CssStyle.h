#pragma once

/**
 * Text alignment values supported by the reader
 */
enum class TextAlign {
  None,    // Default/inherit
  Left,    // Left alignment
  Right,   // Right alignment
  Center,  // Center alignment
  Justify  // Justified text
};

/**
 * Font style values (italic)
 */
enum class CssFontStyle {
  Normal,  // Default normal style
  Italic   // Italic text
};

/**
 * Font weight values (bold)
 */
enum class CssFontWeight {
  Normal,  // Default normal weight
  Bold     // Bold text (700+)
};

/**
 * Text direction (LTR/RTL)
 */
enum class TextDirection {
  Ltr,  // Left-to-right (default)
  Rtl   // Right-to-left (Arabic, Hebrew)
};

/**
 * CssStyle - Represents supported CSS properties for a selector
 *
 * Supported properties:
 * - text-align: left, right, center, justify
 * - font-style: normal, italic
 * - font-weight: normal, bold (700+)
 * - direction: ltr, rtl
 */
struct CssStyle {
  TextAlign textAlign = TextAlign::None;
  bool hasTextAlign = false;

  CssFontStyle fontStyle = CssFontStyle::Normal;
  bool hasFontStyle = false;

  CssFontWeight fontWeight = CssFontWeight::Normal;
  bool hasFontWeight = false;

  TextDirection direction = TextDirection::Ltr;
  bool hasDirection = false;

  // Merge another style into this one (other style takes precedence)
  void merge(const CssStyle& other) {
    if (other.hasTextAlign) {
      textAlign = other.textAlign;
      hasTextAlign = true;
    }
    if (other.hasFontStyle) {
      fontStyle = other.fontStyle;
      hasFontStyle = true;
    }
    if (other.hasFontWeight) {
      fontWeight = other.fontWeight;
      hasFontWeight = true;
    }
    if (other.hasDirection) {
      direction = other.direction;
      hasDirection = true;
    }
  }

  void reset() {
    textAlign = TextAlign::None;
    hasTextAlign = false;
    fontStyle = CssFontStyle::Normal;
    hasFontStyle = false;
    fontWeight = CssFontWeight::Normal;
    hasFontWeight = false;
    direction = TextDirection::Ltr;
    hasDirection = false;
  }
};
