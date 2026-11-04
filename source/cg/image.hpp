#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace cg {

class ImageData final {
public:
    explicit ImageData(const std::filesystem::path &path, std::uint32_t maxDimension = 16384);

    [[nodiscard]] auto Width() const noexcept -> std::uint32_t { return width_; }
    [[nodiscard]] auto Height() const noexcept -> std::uint32_t { return height_; }
    [[nodiscard]] auto Pixels() const noexcept -> std::span<const std::byte> { return pixels_; }

private:
    std::uint32_t width_{};
    std::uint32_t height_{};
    std::vector<std::byte> pixels_;
};

} // namespace cg
