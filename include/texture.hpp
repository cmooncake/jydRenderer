#pragma once

#include "framebuffer.hpp"
#include "vec.hpp"

#include <filesystem>
#include <vector>

namespace jyd {

class Texture {
public:
    explicit Texture(const std::filesystem::path& filename);

    int width() const {
        return width_;
    }

    int height() const {
        return height_;
    }

    const Color& pixel(int x, int y) const;
    Color sampleNearest(const vec2& uv) const;

private:
    int width_ = 0;
    int height_ = 0;
    std::vector<Color> pixels_;
};

} // namespace jyd
