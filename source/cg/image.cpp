#include "image.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_MAX_DIMENSIONS 16384
#include "../../third_party/stb_image.h"

#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

namespace cg {

ImageData::ImageData(const std::filesystem::path &path, const std::uint32_t maxDimension) {
    // std::filesystem::path keeps Unicode filenames usable on Windows as well.
    std::ifstream file{path, std::ios::binary | std::ios::ate};
    if (!file) {
        throw std::runtime_error{"Cannot open image: " + path.string()};
    }
    const auto size = file.tellg();
    if (size <= 0 || size > std::numeric_limits<int>::max()) {
        throw std::runtime_error{"Invalid image file size: " + path.string()};
    }
    std::vector<stbi_uc> encoded(static_cast<std::size_t>(size));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char *>(encoded.data()), static_cast<std::streamsize>(size))) {
        throw std::runtime_error{"Cannot read image: " + path.string()};
    }

    int width{}, height{}, channels{};
    const auto encodedSize = static_cast<int>(encoded.size());
    if (!stbi_info_from_memory(encoded.data(), encodedSize, &width, &height, &channels)
        || width <= 0 || height <= 0
        || static_cast<std::uint32_t>(width) > maxDimension
        || static_cast<std::uint32_t>(height) > maxDimension) {
        throw std::runtime_error{"Invalid or unsupported image dimensions: " + path.string()};
    }

    const std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> decoded{
        stbi_load_from_memory(encoded.data(), encodedSize, &width, &height, &channels, STBI_rgb_alpha),
        &stbi_image_free
    };
    if (!decoded) {
        const auto *reason = stbi_failure_reason();
        throw std::runtime_error{"Cannot decode image: " + path.string() + ": "
                                 + (reason ? reason : "unknown format")};
    }
    width_ = static_cast<std::uint32_t>(width);
    height_ = static_cast<std::uint32_t>(height);
    const auto byteCount = static_cast<std::size_t>(width_) * height_ * 4;
    const auto *begin = reinterpret_cast<const std::byte *>(decoded.get());
    pixels_.assign(begin, begin + byteCount);
}

} // namespace cg
