#include "framebuffer.hpp"

#include <algorithm>
#include <stdexcept>

namespace jyd {

Framebuffer::Framebuffer(int width, int height)
    : width_(width), height_(height), pixels_(static_cast<std::size_t>(width) * height * 4, 0){
    if (width <= 0 || height <= 0) {
        throw std::invalid_argument("Framebuffer dimensions must be positive");
    }
}

void Framebuffer::clear(const Color& color) {
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            setPixel(x, y, color);
        }
    }
}

void Framebuffer::setPixel(int x, int y, const Color& color) {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) {
        return;
    }

    const std::size_t index = (static_cast<std::size_t>(y) * width_ + x) * 4;
    pixels_[index + 0] = color.r;
    pixels_[index + 1] = color.g;
    pixels_[index + 2] = color.b;
    pixels_[index + 3] = color.a;
}

Color Framebuffer::getPixel(int x, int y) const {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) {
        return {};
    }

    const std::size_t index = (static_cast<std::size_t>(y) * width_ + x) * 4;
    return {
        pixels_[index + 0],
        pixels_[index + 1],
        pixels_[index + 2],
        pixels_[index + 3],
    };
}

} // namespace jyd
