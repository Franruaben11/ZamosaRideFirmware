// Code authored by Dean Edis (DeanTheCoder).
// Anyone is free to copy, modify, use, compile, or distribute this software,
// either in source code form or as a compiled binary, for any purpose.
//
// If you modify the code, please retain this copyright header,
// and consider contributing back to the repository or letting us know
// about your modifications. Your contributions are valued!
//
// THE SOFTWARE IS PROVIDED AS IS, WITHOUT WARRANTY OF ANY KIND.

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ZamosaRideGlyph {
    char ch;
    uint8_t sizeId;
    uint16_t x;
    uint16_t y;
    uint8_t w;
    uint8_t h;
    int8_t offsetX;
    int8_t offsetY;
    uint8_t advance;
    uint8_t ascent;
} ZamosaRideGlyph;

extern const uint16_t ZamosaRideFontAtlasWidth;
extern const uint16_t ZamosaRideFontAtlasHeight;
extern const uint32_t ZamosaRideFontAtlasPixelCount;
extern const uint8_t ZamosaRideFontAtlasAlpha[];
extern const ZamosaRideGlyph ZamosaRideFontGlyphs[];
extern const uint16_t ZamosaRideFontGlyphCount;

#ifdef __cplusplus
}
#endif
