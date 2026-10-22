#pragma once

// 128×64 monochrome framebuffer in SSD1306 page layout (8 vertical pixels per
// byte, bit 0 on top), so it can be sent to the panel as-is. Includes a 5×7
// font with descenders in 6×8 cells (21 characters per line).

#include <cstddef>
#include <cstdint>
#include <string>

namespace ui {

class Framebuffer {
public:
    static constexpr int WIDTH  = 128;
    static constexpr int HEIGHT = 64;
    static constexpr int CHAR_W = 6;  // 5 px glyph + 1 px spacing
    static constexpr int CHAR_H = 8;

    void clear();
    void set_pixel(int x, int y, bool on);
    bool pixel(int x, int y) const;
    void fill_rect(int x, int y, int w, int h, bool on);
    void rect(int x, int y, int w, int h, bool on);  // 1 px outline

    // Draws ASCII text with its top-left corner at (x, y); `on` = ink colour,
    // so on=false draws dark text on a filled bar. Returns the width drawn.
    int text(int x, int y, const char* s, bool on = true);
    static int text_width(const char* s);

    const uint8_t* data() const { return buf_; }
    static constexpr size_t SIZE = WIDTH * HEIGHT / 8;

    // '#' / '.' rows — for tests and screen previews on the host
    std::string to_ascii() const;

private:
    uint8_t buf_[SIZE] = {};
};

}  // namespace ui
