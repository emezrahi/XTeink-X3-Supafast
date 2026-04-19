// Unit tests for PageImage::render() clearArea behavior

#include "test_utils.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// Tracking mock for GfxRenderer
struct ClearAreaCall {
  int x, y, width, height;
  uint8_t color;
};

class GfxRenderer {
 public:
  mutable std::vector<ClearAreaCall> clearAreaCalls;

  void clearArea(int x, int y, int width, int height, uint8_t color) const {
    clearAreaCalls.push_back({x, y, width, height, color});
  }
};

// Tracking mock for ImageBlock render
struct RenderCall {
  int fontId, x, y;
};

static std::vector<RenderCall> imageBlockRenderCalls;

// Minimal Block base (matches lib/Epub/Epub/blocks/Block.h)
typedef enum { TEXT_BLOCK, IMAGE_BLOCK } BlockType;

class Block {
 public:
  virtual ~Block() = default;
  virtual void layout(GfxRenderer&) = 0;
  virtual BlockType getType() = 0;
  virtual bool isEmpty() = 0;
};

// Mock ImageBlock with render tracking
class ImageBlock final : public Block {
  uint16_t width_;
  uint16_t height_;

 public:
  ImageBlock(uint16_t w, uint16_t h) : width_(w), height_(h) {}
  BlockType getType() override { return IMAGE_BLOCK; }
  bool isEmpty() override { return false; }
  void layout(GfxRenderer&) override {}
  uint16_t getWidth() const { return width_; }
  uint16_t getHeight() const { return height_; }

  void render(GfxRenderer&, int fontId, int x, int y) const {
    imageBlockRenderCalls.push_back({fontId, x, y});
  }
};

// PageImage under test - mirrors real implementation from Page.cpp
class PageImage {
  std::shared_ptr<ImageBlock> block;

 public:
  int16_t xPos;
  int16_t yPos;

  PageImage(std::shared_ptr<ImageBlock> block, int16_t x, int16_t y)
      : block(std::move(block)), xPos(x), yPos(y) {}

  void render(GfxRenderer& renderer, int fontId, int xOffset, int yOffset, bool black = true) {
    if (!black) {
      renderer.clearArea(xPos + xOffset, yPos + yOffset, block->getWidth(), block->getHeight(), 0xFF);
    }
    block->render(renderer, fontId, xPos + xOffset, yPos + yOffset);
  }
};

int main() {
  TestUtils::TestRunner runner("PageImageRender");

  // --- black=true: no clearArea, block->render still called ---
  {
    GfxRenderer renderer;
    auto block = std::make_shared<ImageBlock>(100, 50);
    PageImage img(block, 10, 20);
    imageBlockRenderCalls.clear();

    img.render(renderer, 42, 5, 3, true);

    runner.expectEq(0, (int)renderer.clearAreaCalls.size(), "black=true: no clearArea call");
    runner.expectEq(1, (int)imageBlockRenderCalls.size(), "black=true: block->render called");
    runner.expectEq(42, imageBlockRenderCalls[0].fontId, "black=true: correct fontId");
    runner.expectEq(15, imageBlockRenderCalls[0].x, "black=true: x = xPos(10) + xOffset(5)");
    runner.expectEq(23, imageBlockRenderCalls[0].y, "black=true: y = yPos(20) + yOffset(3)");
  }

  // --- black=false: clearArea called before block->render ---
  {
    GfxRenderer renderer;
    auto block = std::make_shared<ImageBlock>(100, 50);
    PageImage img(block, 10, 20);
    imageBlockRenderCalls.clear();

    img.render(renderer, 42, 5, 3, false);

    runner.expectEq(1, (int)renderer.clearAreaCalls.size(), "black=false: clearArea called");
    runner.expectEq(15, renderer.clearAreaCalls[0].x, "clearArea x = xPos(10) + xOffset(5)");
    runner.expectEq(23, renderer.clearAreaCalls[0].y, "clearArea y = yPos(20) + yOffset(3)");
    runner.expectEq(100, renderer.clearAreaCalls[0].width, "clearArea width = block width");
    runner.expectEq(50, renderer.clearAreaCalls[0].height, "clearArea height = block height");
    runner.expectEq((uint8_t)0xFF, renderer.clearAreaCalls[0].color, "clearArea color = 0xFF (white)");

    runner.expectEq(1, (int)imageBlockRenderCalls.size(), "black=false: block->render called");
    runner.expectEq(15, imageBlockRenderCalls[0].x, "black=false: render x correct");
    runner.expectEq(23, imageBlockRenderCalls[0].y, "black=false: render y correct");
  }

  // --- default parameter (black=true) ---
  {
    GfxRenderer renderer;
    auto block = std::make_shared<ImageBlock>(64, 64);
    PageImage img(block, 0, 0);
    imageBlockRenderCalls.clear();

    img.render(renderer, 1, 0, 0);  // default black=true

    runner.expectEq(0, (int)renderer.clearAreaCalls.size(), "default: no clearArea");
    runner.expectEq(1, (int)imageBlockRenderCalls.size(), "default: block->render called");
  }

  // --- zero offset ---
  {
    GfxRenderer renderer;
    auto block = std::make_shared<ImageBlock>(200, 150);
    PageImage img(block, 30, 40);
    imageBlockRenderCalls.clear();

    img.render(renderer, 0, 0, 0, false);

    runner.expectEq(30, renderer.clearAreaCalls[0].x, "zero offset: clearArea x = xPos");
    runner.expectEq(40, renderer.clearAreaCalls[0].y, "zero offset: clearArea y = yPos");
    runner.expectEq(200, renderer.clearAreaCalls[0].width, "zero offset: clearArea width");
    runner.expectEq(150, renderer.clearAreaCalls[0].height, "zero offset: clearArea height");
  }

  return runner.allPassed() ? 0 : 1;
}
