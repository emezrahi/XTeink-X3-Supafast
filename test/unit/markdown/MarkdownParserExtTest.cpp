#include "test_utils.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
#include "md_parser.h"
}

struct TokenCollector {
  std::vector<md_token_type_t> types;
  std::vector<std::string> texts;
  std::vector<uint8_t> data;

  void clear() {
    types.clear();
    texts.clear();
    data.clear();
  }
};

static bool collectTokens(const md_token_t* token, void* userData) {
  TokenCollector* collector = static_cast<TokenCollector*>(userData);
  collector->types.push_back(token->type);
  if (token->text && token->length > 0) {
    collector->texts.push_back(std::string(token->text, token->length));
  } else {
    collector->texts.push_back("");
  }
  collector->data.push_back(token->data);
  return true;
}

static bool hasTokenType(const TokenCollector& c, md_token_type_t type) {
  for (auto t : c.types) {
    if (t == type) return true;
  }
  return false;
}

static std::string getFirstTextOfType(const TokenCollector& c, md_token_type_t type) {
  for (size_t i = 0; i < c.types.size(); i++) {
    if (c.types[i] == type) return c.texts[i];
  }
  return "";
}

static int countTokenType(const TokenCollector& c, md_token_type_t type) {
  int count = 0;
  for (auto t : c.types) {
    if (t == type) count++;
  }
  return count;
}

static std::string getAllTextBetween(const TokenCollector& c, md_token_type_t startType,
                                    md_token_type_t endType) {
  std::string result;
  bool inside = false;
  for (size_t i = 0; i < c.types.size(); i++) {
    if (c.types[i] == startType) {
      inside = true;
      continue;
    }
    if (c.types[i] == endType) {
      inside = false;
      continue;
    }
    if (inside && c.types[i] == MD_TEXT) {
      result += c.texts[i];
    }
  }
  return result;
}

int main() {
  TestUtils::TestRunner runner("Markdown Parser Extended");

  TokenCollector collector;
  md_parser_t parser;

  // ============================================
  // 1. Nested bold+italic: ***bold italic***
  // ============================================
  {
    collector.clear();
    md_parser_init(&parser, collectTokens, &collector);
    md_parse(&parser, "***bold italic***", 17);

    runner.expectTrue(hasTokenType(collector, MD_BOLD_START),
                      "Nested bold+italic: has BOLD_START");
    runner.expectTrue(hasTokenType(collector, MD_ITALIC_START),
                      "Nested bold+italic: has ITALIC_START");
    runner.expectTrue(hasTokenType(collector, MD_BOLD_END),
                      "Nested bold+italic: has BOLD_END");
    runner.expectTrue(hasTokenType(collector, MD_ITALIC_END),
                      "Nested bold+italic: has ITALIC_END");

    std::string text;
    for (size_t i = 0; i < collector.types.size(); i++) {
      if (collector.types[i] == MD_TEXT) text += collector.texts[i];
    }
    runner.expectTrue(text.find("bold italic") != std::string::npos,
                      "Nested bold+italic: text contains 'bold italic'");
  }

  // ============================================
  // 2. Code block content emitted as TEXT
  // ============================================
  {
    collector.clear();
    md_parser_init(&parser, collectTokens, &collector);
    const char* input = "```\nline1\nline2\n```";
    md_parse(&parser, input, strlen(input));

    runner.expectTrue(hasTokenType(collector, MD_CODE_BLOCK_START),
                      "Code block content: has CODE_BLOCK_START");
    runner.expectTrue(hasTokenType(collector, MD_CODE_BLOCK_END),
                      "Code block content: has CODE_BLOCK_END");

    std::string codeText = getAllTextBetween(collector, MD_CODE_BLOCK_START, MD_CODE_BLOCK_END);
    runner.expectTrue(codeText.find("line1") != std::string::npos,
                      "Code block content: contains 'line1'");
    runner.expectTrue(codeText.find("line2") != std::string::npos,
                      "Code block content: contains 'line2'");
  }

  // ============================================
  // 3. Inline code with special chars
  // ============================================
  {
    collector.clear();
    md_parser_init(&parser, collectTokens, &collector);
    md_parse(&parser, "`a < b`", 7);

    runner.expectTrue(hasTokenType(collector, MD_CODE_INLINE),
                      "Inline code special: has CODE_INLINE");
    std::string code = getFirstTextOfType(collector, MD_CODE_INLINE);
    runner.expectEqual("a < b", code, "Inline code special: text is 'a < b'");
  }

  // ============================================
  // 4. Unordered list with +
  // ============================================
  {
    collector.clear();
    md_parser_init(&parser, collectTokens, &collector);
    md_parse(&parser, "+ item", 6);

    runner.expectTrue(hasTokenType(collector, MD_LIST_ITEM_START),
                      "Unordered list +: has LIST_ITEM_START");
    for (size_t i = 0; i < collector.types.size(); i++) {
      if (collector.types[i] == MD_LIST_ITEM_START) {
        runner.expectEq(static_cast<uint8_t>(0), collector.data[i],
                        "Unordered list +: data is 0");
        break;
      }
    }
  }

  // ============================================
  // 5. Ordered list multi-digit
  // ============================================
  {
    collector.clear();
    md_parser_init(&parser, collectTokens, &collector);
    md_parse(&parser, "99. item", 8);

    runner.expectTrue(hasTokenType(collector, MD_LIST_ITEM_START),
                      "Ordered list 99: has LIST_ITEM_START");
    for (size_t i = 0; i < collector.types.size(); i++) {
      if (collector.types[i] == MD_LIST_ITEM_START) {
        runner.expectEq(static_cast<uint8_t>(99), collector.data[i],
                        "Ordered list 99: data is 99");
        break;
      }
    }
  }

  // ============================================
  // 6. Blockquote content
  // ============================================
  {
    collector.clear();
    md_parser_init(&parser, collectTokens, &collector);
    md_parse(&parser, "> quoted text", 13);

    runner.expectTrue(hasTokenType(collector, MD_BLOCKQUOTE_START),
                      "Blockquote content: has BLOCKQUOTE_START");

    std::string text = getAllTextBetween(collector, MD_BLOCKQUOTE_START, MD_BLOCKQUOTE_END);
    runner.expectTrue(text.find("quoted text") != std::string::npos,
                      "Blockquote content: text contains 'quoted text'");
  }

  // ============================================
  // 7. Empty lines between paragraphs
  // ============================================
  {
    collector.clear();
    md_parser_init(&parser, collectTokens, &collector);
    const char* input = "para1\n\npara2";
    md_parse(&parser, input, strlen(input));

    int newlineCount = countTokenType(collector, MD_NEWLINE);
    runner.expectTrue(newlineCount >= 2, "Empty lines: at least 2 NEWLINE tokens");
  }

  // ============================================
  // 8. Unclosed bold at EOF
  // ============================================
  {
    collector.clear();
    md_parser_init(&parser, collectTokens, &collector);
    md_parse(&parser, "**no close", 10);

    runner.expectTrue(hasTokenType(collector, MD_BOLD_START),
                      "Unclosed bold EOF: has BOLD_START");
    runner.expectTrue(hasTokenType(collector, MD_BOLD_END),
                      "Unclosed bold EOF: auto-closed with BOLD_END");
  }

  // ============================================
  // 9. Header with no text
  // ============================================
  {
    collector.clear();
    md_parser_init(&parser, collectTokens, &collector);
    md_parse(&parser, "# ", 2);

    runner.expectTrue(hasTokenType(collector, MD_HEADER_START),
                      "Empty header: has HEADER_START");
    runner.expectTrue(hasTokenType(collector, MD_HEADER_END),
                      "Empty header: has HEADER_END");
    for (size_t i = 0; i < collector.types.size(); i++) {
      if (collector.types[i] == MD_HEADER_START) {
        runner.expectEq(static_cast<uint8_t>(1), collector.data[i],
                        "Empty header: level is 1");
        break;
      }
    }
  }

  // ============================================
  // 10. Long line (600 chars)
  // ============================================
  {
    collector.clear();
    md_parser_init(&parser, collectTokens, &collector);
    std::string longLine = "# ";
    longLine.append(598, 'A');
    md_parse(&parser, longLine.c_str(), longLine.size());

    runner.expectTrue(hasTokenType(collector, MD_HEADER_START),
                      "Long line: has HEADER_START (no crash)");
  }

  // ============================================
  // 11. Strikethrough content
  // ============================================
  {
    collector.clear();
    md_parser_init(&parser, collectTokens, &collector);
    md_parse(&parser, "~~struck~~", 10);

    runner.expectTrue(hasTokenType(collector, MD_STRIKE_START),
                      "Strikethrough: has STRIKE_START");
    runner.expectTrue(hasTokenType(collector, MD_STRIKE_END),
                      "Strikethrough: has STRIKE_END");

    std::string text = getAllTextBetween(collector, MD_STRIKE_START, MD_STRIKE_END);
    runner.expectEqual("struck", text, "Strikethrough: text is 'struck'");
  }

  // ============================================
  // 12. Mixed inline formatting
  // ============================================
  {
    collector.clear();
    md_parser_init(&parser, collectTokens, &collector);
    const char* input = "normal **bold *bolditalic* bold** normal";
    md_parse(&parser, input, strlen(input));

    runner.expectTrue(hasTokenType(collector, MD_BOLD_START),
                      "Mixed inline: has BOLD_START");
    runner.expectTrue(hasTokenType(collector, MD_ITALIC_START),
                      "Mixed inline: has ITALIC_START");
  }

  // ============================================
  // 13. Image with alt text
  // ============================================
  {
    collector.clear();
    md_parser_init(&parser, collectTokens, &collector);
    md_parse(&parser, "![my alt](pic.png)", 18);

    runner.expectTrue(hasTokenType(collector, MD_IMAGE_ALT_START),
                      "Image alt: has IMAGE_ALT_START");
    runner.expectTrue(hasTokenType(collector, MD_IMAGE_ALT_END),
                      "Image alt: has IMAGE_ALT_END");
    runner.expectTrue(hasTokenType(collector, MD_IMAGE_URL),
                      "Image alt: has IMAGE_URL");

    std::string altText = getAllTextBetween(collector, MD_IMAGE_ALT_START, MD_IMAGE_ALT_END);
    runner.expectEqual("my alt", altText, "Image alt: alt text is 'my alt'");

    std::string url = getFirstTextOfType(collector, MD_IMAGE_URL);
    runner.expectEqual("pic.png", url, "Image alt: URL is 'pic.png'");
  }

  // ============================================
  // 14. Link with empty text
  // ============================================
  {
    collector.clear();
    md_parser_init(&parser, collectTokens, &collector);
    const char* input = "[](http://example.com)";
    md_parse(&parser, input, strlen(input));

    runner.expectTrue(hasTokenType(collector, MD_LINK_TEXT_START),
                      "Empty link text: has LINK_TEXT_START");
    runner.expectTrue(hasTokenType(collector, MD_LINK_TEXT_END),
                      "Empty link text: has LINK_TEXT_END");
    runner.expectTrue(hasTokenType(collector, MD_LINK_URL),
                      "Empty link text: has LINK_URL");

    std::string url = getFirstTextOfType(collector, MD_LINK_URL);
    runner.expectEqual("http://example.com", url,
                       "Empty link text: URL is 'http://example.com'");
  }

  // ============================================
  // 15. Parser reset between parses
  // ============================================
  {
    collector.clear();
    md_parser_init(&parser, collectTokens, &collector);
    md_parse(&parser, "**bold", 6);

    collector.clear();
    md_parser_reset(&parser);
    md_parse(&parser, "normal", 6);

    runner.expectFalse(hasTokenType(collector, MD_BOLD_START),
                       "Reset isolation: no BOLD_START in second parse");
    runner.expectFalse(hasTokenType(collector, MD_BOLD_END),
                       "Reset isolation: no BOLD_END in second parse");
    runner.expectTrue(hasTokenType(collector, MD_TEXT),
                      "Reset isolation: has TEXT in second parse");
  }

  return runner.allPassed() ? 0 : 1;
}
