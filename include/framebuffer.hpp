#pragma once

#include <cstdint>
#include <vector>

namespace jyd {

struct Color {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;
};

class Framebuffer {
public:
    Framebuffer(int width, int height);

    int width() const { return width_; }
    int height() const { return height_; }

    void clear(const Color& color);
    void setPixel(int x, int y, const Color& color);
    Color getPixel(int x, int y) const;

    const std::uint8_t* data() const { return pixels_.data(); }
    std::uint8_t* data() { return pixels_.data(); }
    std::size_t byteSize() const { return pixels_.size(); }

private:
    int width_;
    int height_;
    std::vector<std::uint8_t> pixels_;
};

} // namespace jyd
