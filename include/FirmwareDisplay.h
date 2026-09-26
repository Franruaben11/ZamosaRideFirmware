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

#include "ZamosaRide/Display.h"
#include "ZamosaRide/FontAtlas.h"

#include "esp_lcd_panel_ops.h"

#include <cstdint>

class FirmwareDisplay final : public ZamosaRide::Display {
public:
    bool begin();
    void splash(uint8_t opacity);
    void setAwake(bool awake);
    bool isAwake() const;

    int width() const override;
    int height() const override;

    void clear(ZamosaRide::Color color) override;
    void present() override;

    void pixel(int x, int y, ZamosaRide::Color color) override;
    void line(int x0, int y0, int x1, int y1, ZamosaRide::Color color, int thickness = 1) override;
    void circle(int cx, int cy, int radius, ZamosaRide::Color color, int thickness = 1) override;
    void arc(int cx, int cy, int radius, float startDegrees, float sweepDegrees, ZamosaRide::Color color, int thickness = 1) override;
    void fillCircle(int cx, int cy, int radius, ZamosaRide::Color color) override;
    void image(int x, int y, const ZamosaRideGrayAlphaImage& image, uint8_t opacity = 255) override;
    void text(int x, int y, const char* value, int size, ZamosaRide::Color color, ZamosaRide::TextAlign align = ZamosaRide::TextAlign::Left) override;

private:
    static constexpr int LcdWidth = 466;
    static constexpr int LcdHeight = 466;
    static constexpr int LcdColorBits = 16;
    static constexpr int LcdTePin = 13;
    static constexpr int LcdSckPin = 38;
    static constexpr int LcdData0Pin = 4;
    static constexpr int LcdData1Pin = 5;
    static constexpr int LcdData2Pin = 6;
    static constexpr int LcdData3Pin = 7;
    static constexpr int LcdCsPin = 12;
    static constexpr int LcdRstPin = 39;
    static constexpr int LcdSpiMaxTransferSize = 466 * 80 * 2;

    esp_lcd_panel_handle_t _panel = nullptr;
    uint16_t* _frame = nullptr;
    bool _awake = false;

    uint16_t rgb565(ZamosaRide::Color color) const;
    void putPixel(int x, int y, uint16_t color);
    void blendPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha);
    void maxPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha);
    void imageCentered(uint8_t opacity);
    const ZamosaRideGlyph* glyphFor(char value, int size) const;
    int textWidth(const char* value, int size) const;
};
