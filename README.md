# Pixelpaper

Pixelpaper is a highly optimized, feature-rich open-source firmware for the Xteink X4 e-paper reader, focused on instant usability and minimal waiting times.

## Key Features

- **Lightning-fast navigation:** Open books, turn pages, and browse your library with almost no waiting times, thanks to deep code and rendering optimizations.
- **Wide format support:** Read EPUB2/EPUB3, FB2, HTML, Markdown, TXT, and XTC/XTCH files.
- **Instant search and TOC:** Table of contents and bookmarks are available immediately, even for large books.
- **Customizable display:** Change fonts, font sizes, text alignment, and themes on the fly. Supports custom fonts and themes from SD card.
- **Advanced text layout:** True hyphenation, justified/left/center/right alignment, CJK, Thai, Arabic, and more. TeX-quality line breaking.
- **Image support:** Book covers and in-book images (JPEG, PNG, BMP) render quickly and smoothly.
- **No-lag UI:** All menus, dialogs, and settings are instantly responsive.
- **WiFi file transfer:** Upload books wirelessly via the built-in web server.
- **Calibre integration:** Send books directly from Calibre desktop.
- **Power optimizations:** Sunlight fading fix, efficient sleep modes, and low idle power.
- **File explorer:** Fast, nested folder navigation with exFAT/FAT32 and UTF-8 filename support.
- **One-handed reading:** Button remapping and power button page turn.
- **Maintenance tools:** Cleanup, system info, and factory reset options.

## Performance & Optimization

- **Minimal waiting times:** All core operations are optimized for speed. Most actions are instant or near-instant, even with large libraries.
- **Efficient memory use:** Designed for the ESP32-C3’s constraints, with careful caching and resource management.
- **Optimized rendering pipeline:** Custom font and image rendering for e-paper, with anti-aliasing and grayscale options.
- **No bloat:** Only essential features are included, keeping the firmware lightweight and fast.

## Flashing your device

- Download firmware.bin from https://codeberg.org/potentialuselessness/PIXELPAGES/releases
- Connect the device by USB-C.
- Wake or reboot it if needed.
- Open xteink.dve.al.
- OTA flash the firmware.bin

## Internals

Pixelpaper is designed for the ESP32-C3's ~380KB RAM constraint. See [docs/architecture.md](docs/architecture.md) for detailed architecture documentation.

### Data caching

The first time chapters of a book are loaded, they are cached to the SD card. Subsequent loads are served from the cache. This cache directory exists at `.pixelpaper` on the SD card. The structure is as follows:


```
.pixelpaper/
├── epub_12471232/       # Each EPUB is cached to a subdirectory named `epub_<hash>`
│   ├── progress.bin     # Stores reading progress (chapter, page, etc.)
│   ├── bookmarks.bin    # Saved bookmarks (up to 20 per book)
│   ├── bookmarks.txt    # Human-readable bookmark list (companion to bookmarks.bin)
│   ├── cover.bmp        # Book cover image (once generated)
│   ├── book.bin         # Book metadata (title, author, spine, table of contents, etc.)
│   ├── sections/        # All chapter data is stored in the sections subdirectory
│   │   ├── 0.bin        # Chapter data (screen count, all text layout info, etc.)
│   │   ├── 1.bin        #     files are named by their index in the spine
│   │   └── ...
│   └── images/          # Cached inline images (converted to 2-bit BMP)
│       ├── 123456.bmp   # Images named by hash of source path
│       └── ...
│
├── fb2_55667788/        # Each FB2 file is cached to a subdirectory named `fb2_<hash>`
│   ├── meta.bin         # Cached metadata (title, author, TOC) for faster reloads
│   ├── progress.bin     # Stores reading progress
│   ├── cover.bmp        # Cover image (converted from adjacent image file)
│   ├── sections/        # Cached chapter pages (same format as EPUB sections)
│   │   ├── 0.bin
│   │   └── ...
│
│
├── txt_98765432/        # Each TXT file is cached to a subdirectory named `txt_<hash>`
│   ├── progress.bin     # Stores current page number (4-byte uint32)
│   ├── index.bin        # Page index (byte offsets for each page start)
│   └── cover.bmp        # Cover image (converted from book.jpg/png/bmp or cover.jpg/png/bmp)
│
├── md_12345678/         # Each Markdown file is cached to a subdirectory named `md_<hash>`
│   ├── progress.bin     # Stores current page number (2-byte uint16)
│   ├── section.bin      # Parsed pages (same format as EPUB sections)
│   └── cover.bmp        # Cover image (converted from README.jpg/png/bmp or cover.jpg/png/bmp)
│
├── html_12345678/       # Each HTML file is cached to a subdirectory named `html_<hash>`
│   ├── progress.bin     # Stores current page number (4-byte, same as TXT/Markdown)
│   ├── pages_<fontId>.bin  # Parsed pages (same format as Markdown/FB2 sections)
│   └── cover.bmp        # Cover image (converted from adjacent image file)
│
└── epub_189013891/
```

To clear cached data, use **Settings > Cleanup** (see [User Guide](docs/user_guide.md)). Alternatively, delete the `.pixelpaper` directory manually.

Due the way it's currently implemented, the cache is not automatically cleared when a book is deleted and moving a book file will use a new cache directory, resetting the reading progress.

For more details on the internal file structures, see the [file formats document](./docs/file-formats.md).

## Related Tools

### EPUB to XTC Converter (Web)

[epub-to-xtc-converter](https://github.com/bigbag/epub-to-xtc-converter) — browser-based converter from EPUB to Xteink's native XTC/XTCH format. Uses CREngine WASM for accurate rendering.

- Device presets for Xteink X4/X3 (480x800)
- Font selection from Google Fonts or custom TTF/OTF
- Configurable margins, line height, hyphenation (42 languages)
- Dark mode and dithering options
- Batch processing and ZIP export

**Live version:** [liashkov.site/epub-to-xtc-converter](https://liashkov.site/epub-to-xtc-converter/)

### EPUB Optimizer (CLI)

[xteink-epub-optimizer](https://github.com/bigbag/xteink-epub-optimizer) — command-line tool to optimize EPUB files for the Xteink X4's constraints (480×800 display, limited RAM):

- **CSS Sanitization** - Removes complex layouts (floats, flexbox, grid)
- **Font Removal** - Strips embedded fonts to reduce file size
- **Image Optimization** - Grayscale conversion, resizing to 480px max width
- **XTC/XTCH Conversion** - Convert EPUBs to Xteink's native format

```bash
# Optimize EPUB
python src/optimizer.py ./ebooks ./optimized

# Convert to XTCH format
python src/converter.py book.epub book.xtch --font fonts/MyFont.ttf
```

## Contributing

Contributions are very welcome!

Pixelpaper is a fork of [CrossPoint Reader](https://github.com/daveallie/crosspoint-reader) by Dave Allie.

X4 hardware insights from [bb_epaper](https://github.com/bitbank2/bb_epaper) by Larry Bank.

Markdown parsing using [MD4C](https://github.com/mity/md4c) by Martin Mitáš.

CSS parser adapted from [microreader](https://github.com/CidVonHighwind/microreader) by CidVonHighwind.

**Not affiliated with Xteink or any manufacturer of the X4 hardware**.
