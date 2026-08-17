#include "texture.hpp"

#include "stb_image.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace jyd {

    Texture::Texture(const std::filesystem::path& filename) {
        // Passing a filesystem path to ifstream preserves Unicode paths on Windows.
        std::ifstream input(filename, std::ios::binary | std::ios::ate);
        if (!input) {
            throw std::runtime_error(
                "Cannot open texture file: " + filename.string());
        }

        const std::streampos fileSize = input.tellg();
        if (fileSize <= 0 || fileSize > INT_MAX) {
            throw std::runtime_error(
                "Invalid or excessively large texture file: " +
                filename.string());
        }

        std::vector<std::uint8_t> encoded(
            static_cast<std::size_t>(fileSize));

        input.seekg(0, std::ios::beg);
        if (!input.read(
            reinterpret_cast<char*>(encoded.data()),
            static_cast<std::streamsize>(encoded.size()))) {
            throw std::runtime_error(
                "Cannot read texture file: " + filename.string());
        }

        int sourceChannels = 0;

        std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> decoded(
            stbi_load_from_memory(
                encoded.data(),
                static_cast<int>(encoded.size()),
                &width_,
                &height_,
                &sourceChannels,
                STBI_rgb_alpha),
            &stbi_image_free);

        if (!decoded) {
            const char* reason = stbi_failure_reason();

            throw std::runtime_error(
                "Cannot decode texture: " + filename.string() +
                (reason ? std::string(" (") + reason + ")" : ""));
        }

        if (width_ <= 0 || height_ <= 0) {
            throw std::runtime_error("Texture dimensions are invalid");
        }

        const std::size_t pixelCount =
            static_cast<std::size_t>(width_) *
            static_cast<std::size_t>(height_);

        pixels_.resize(pixelCount);

        for (std::size_t i = 0; i < pixelCount; ++i) {
            pixels_[i] = {
                decoded.get()[i * 4 + 0],
                decoded.get()[i * 4 + 1],
                decoded.get()[i * 4 + 2],
                decoded.get()[i * 4 + 3]
            };
        }
    }

    const Color& Texture::pixel(int x, int y) const {
        if (x < 0 || y < 0 || x >= width_ || y >= height_) {
            throw std::out_of_range("Texture pixel is out of range");
        }

        const std::size_t index =
            static_cast<std::size_t>(y) *
            static_cast<std::size_t>(width_) +
            static_cast<std::size_t>(x);

        return pixels_[index];
    }

    Color Texture::sampleNearest(const vec2& uv) const {
        const double u = uv[0] - std::floor(uv[0]);
        const double v = uv[1] - std::floor(uv[1]);

        const int x = std::clamp(
            static_cast<int>(u * width_),
            0,
            width_ - 1);

        // Image rows start at the top; OBJ texture V coordinates start at the bottom.
        const int y = std::clamp(
            static_cast<int>((1.0 - v) * height_),
            0,
            height_ - 1);

        return pixel(x, y);
    }

} // namespace jyd
