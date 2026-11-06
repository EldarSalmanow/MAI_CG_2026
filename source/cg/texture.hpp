#pragma once

#include "upload.hpp"

#include <cstddef>
#include <filesystem>
#include <span>
#include <vector>

namespace cg {

    class ImageData {
    public:
        explicit ImageData(
            const std::filesystem::path &path,
            std::uint32_t maxDimension = 16384
        );

    public:
        auto Width() const noexcept -> std::uint32_t {
            return width_;
        }

        auto Height() const noexcept -> std::uint32_t {
            return height_;
        }

        auto Pixels() const noexcept -> std::span<const std::byte> {
            return pixels_;
        }

    private:
        std::uint32_t width_{};
        std::uint32_t height_{};
        std::vector<std::byte> pixels_;
    };

    class Texture {
    public:
        Texture(
            const UploadContext &uploads,
            const ImageData &pixels,
            VkFormat format
        );

    public:
        auto View() const noexcept -> VkImageView {
            return view_.Get();
        }

        auto Levels() const noexcept -> std::uint32_t {
            return mipLevels_;
        }

    private:
        auto UploadPixels(
            const UploadContext &uploads,
            const ImageData &pixels
        ) -> void;

        auto GenerateMipmaps(
            VkCommandBuffer commands,
            std::uint32_t width,
            std::uint32_t height
        ) const -> void;

        auto CreateView(
            VkDevice device,
            VkFormat format
        ) -> void;

        static auto MipLevels(
            VkPhysicalDevice device,
            const ImageData &pixels,
            VkFormat format
        ) -> std::uint32_t;

        static auto ImageInfo(
            const ImageData &pixels,
            VkFormat format,
            std::uint32_t levels
        ) -> VkImageCreateInfo;

        auto Transition(
            VkCommandBuffer commands,
            std::uint32_t base,
            std::uint32_t count,
            VkImageLayout oldLayout,
            VkImageLayout newLayout
        ) const -> void;

    private:
        std::uint32_t mipLevels_;
        DeviceImage image_;
        ImageView view_;
    };

} // namespace cg
