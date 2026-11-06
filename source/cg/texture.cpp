#include "texture.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_MAX_DIMENSIONS 16384
#include "../../third_party/stb_image.h"

#include <bit>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

namespace cg {

    ImageData::ImageData(
        const std::filesystem::path &path,
        const std::uint32_t maxDimension
    ) {
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
        if (!stbi_info_from_memory(encoded.data(), encodedSize, &width, &height, &channels) || width <= 0 ||
            height <= 0 || static_cast<std::uint32_t>(width) > maxDimension ||
            static_cast<std::uint32_t>(height) > maxDimension) {
            throw std::runtime_error{"Invalid or unsupported image dimensions: " + path.string()};
        }

        const std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> decoded{
            stbi_load_from_memory(encoded.data(), encodedSize, &width, &height, &channels, STBI_rgb_alpha),
            &stbi_image_free
        };
        if (!decoded) {
            const auto *reason = stbi_failure_reason();
            throw std::runtime_error{
                "Cannot decode image: " + path.string() + ": " + (reason ? reason : "unknown format")
            };
        }
        width_ = static_cast<std::uint32_t>(width);
        height_ = static_cast<std::uint32_t>(height);
        const auto byteCount = static_cast<std::size_t>(width_) * height_ * 4;
        const auto *begin = reinterpret_cast<const std::byte *>(decoded.get());
        pixels_.assign(begin, begin + byteCount);
    }

    Texture::Texture(
        const UploadContext &uploads,
        const ImageData &pixels,
        const VkFormat format
    )
        : mipLevels_{MipLevels(uploads.Context().physical_device, pixels, format)},
          image_{uploads.Context().allocator, ImageInfo(pixels, format, mipLevels_)},
          view_{uploads.Context().device} {
        UploadPixels(uploads, pixels);
        CreateView(uploads.Context().device, format);
    }

    auto Texture::UploadPixels(
        const UploadContext &uploads,
        const ImageData &pixels
    ) -> void {
        Buffer staging{uploads.Context().allocator, pixels.Pixels().size_bytes(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT};
        staging.Upload(pixels.Pixels());

        uploads.Submit([&](const VkCommandBuffer commands) {
            Transition(commands, 0, mipLevels_, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

            const VkBufferImageCopy copy{
                .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                .imageExtent = {pixels.Width(), pixels.Height(), 1}
            };
            vkCmdCopyBufferToImage(
                commands, staging.Get(), image_.Get(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy
            );

            GenerateMipmaps(commands, pixels.Width(), pixels.Height());
        });
    }

    auto Texture::GenerateMipmaps(
        const VkCommandBuffer commands,
        const std::uint32_t width,
        const std::uint32_t height
    ) const -> void {
        auto sourceWidth = static_cast<std::int32_t>(width);
        auto sourceHeight = static_cast<std::int32_t>(height);

        for (std::uint32_t level = 1; level < mipLevels_; ++level) {
            Transition(
                commands, level - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
            );

            const auto nextWidth = std::max(1, sourceWidth / 2);
            const auto nextHeight = std::max(1, sourceHeight / 2);
            const VkImageBlit blit{
                .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 0, 1},
                .srcOffsets = {{0, 0, 0}, {sourceWidth, sourceHeight, 1}},
                .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1},
                .dstOffsets = {{0, 0, 0}, {nextWidth, nextHeight, 1}}
            };
            vkCmdBlitImage(
                commands, image_.Get(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image_.Get(),
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR
            );

            Transition(
                commands, level - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            );

            sourceWidth = nextWidth;
            sourceHeight = nextHeight;
        }

        Transition(
            commands, mipLevels_ - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        );
    }

    auto Texture::CreateView(
        const VkDevice device,
        const VkFormat format
    ) -> void {
        const VkImageViewCreateInfo info{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = image_.Get(),
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = format,
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels_, 0, 1}
        };

        CheckVulkan(vkCreateImageView(device, &info, nullptr, view_.Put()), "vkCreateImageView");
    }

    auto Texture::MipLevels(
        const VkPhysicalDevice device,
        const ImageData &pixels,
        const VkFormat format
    ) -> std::uint32_t {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(device, format, &properties);

        constexpr auto samplingFeatures =
            VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
        if ((properties.optimalTilingFeatures & samplingFeatures) != samplingFeatures) {
            throw std::runtime_error{"Texture format does not support linear sampling"};
        }

        constexpr auto blitFeatures = VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT;
        if ((properties.optimalTilingFeatures & blitFeatures) != blitFeatures) {
            return 1;
        }

        return std::bit_width(std::max(pixels.Width(), pixels.Height()));
    }

    auto Texture::ImageInfo(
        const ImageData &pixels,
        const VkFormat format,
        const std::uint32_t levels
    ) -> VkImageCreateInfo {
        return {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = format,
            .extent = {pixels.Width(), pixels.Height(), 1},
            .mipLevels = levels,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
        };
    }

    auto Texture::Transition(
        const VkCommandBuffer commands,
        const std::uint32_t base,
        const std::uint32_t count,
        const VkImageLayout oldLayout,
        const VkImageLayout newLayout
    ) const -> void {
        VkPipelineStageFlags sourceStage;
        VkPipelineStageFlags destinationStage;
        VkAccessFlags sourceAccess;
        VkAccessFlags destinationAccess;

        if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            sourceAccess = 0;
            destinationAccess = VK_ACCESS_TRANSFER_WRITE_BIT;
        } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
                   newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
            sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            sourceAccess = VK_ACCESS_TRANSFER_WRITE_BIT;
            destinationAccess = VK_ACCESS_TRANSFER_READ_BIT;
        } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL &&
                   newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            sourceAccess = VK_ACCESS_TRANSFER_READ_BIT;
            destinationAccess = VK_ACCESS_SHADER_READ_BIT;
        } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
                   newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            sourceAccess = VK_ACCESS_TRANSFER_WRITE_BIT;
            destinationAccess = VK_ACCESS_SHADER_READ_BIT;
        } else {
            throw std::invalid_argument{"Unsupported texture layout transition"};
        }

        const VkImageMemoryBarrier barrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = sourceAccess,
            .dstAccessMask = destinationAccess,
            .oldLayout = oldLayout,
            .newLayout = newLayout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image_.Get(),
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, base, count, 0, 1}
        };

        vkCmdPipelineBarrier(commands, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

} // namespace cg
