#pragma once

#include <Xtc/XtcParser.h>

#include <string>

namespace xtc {

// Generate 1-bit BMP cover from page 0 of an XTC/XTCH file.
// Caller must ensure parent directory exists.
// Returns true on success.
bool generateCoverBmpFromParser(XtcParser& parser, const std::string& coverBmpPath);

}  // namespace xtc
