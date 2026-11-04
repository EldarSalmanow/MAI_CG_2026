#pragma once

#include "image.hpp"
#include "upload.hpp"

#include <bit>

namespace cg {

class DeviceImage final {
public:
    DeviceImage(const VmaAllocator allocator, const VkImageCreateInfo &info) : allocator_{allocator} {
        const VmaAllocationCreateInfo allocationInfo{
            .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
            .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        };
        CheckVulkan(vmaCreateImage(allocator_, &info, &allocationInfo, &image_, &allocation_, nullptr), "vmaCreateImage");
    }

    DeviceImage(const DeviceImage &) = delete;
    auto operator=(const DeviceImage &) -> DeviceImage & = delete;

    ~DeviceImage() { vmaDestroyImage(allocator_, image_, allocation_); }

    auto Get() const noexcept -> VkImage { return image_; }

private:
    VmaAllocator allocator_;
    VkImage image_{};
    VmaAllocation allocation_{};
};

class Texture final {
public:
    Texture(const UploadContext &uploads, const ImageData &pixels, const VkFormat format)
        : mipLevels_{MipLevels(uploads.Context().physical_device, pixels, format)},
          image_{uploads.Context().allocator, ImageInfo(pixels, format, mipLevels_)},
          view_{uploads.Context().device} {
        Buffer staging{uploads.Context().allocator, pixels.Pixels().size_bytes(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT};
        staging.Upload(pixels.Pixels());
        uploads.Submit([&](const VkCommandBuffer commands) {
            Transition(commands, 0, mipLevels_, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            const VkBufferImageCopy copy{
                .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                .imageExtent = {pixels.Width(), pixels.Height(), 1}
            };
            vkCmdCopyBufferToImage(commands, staging.Get(), image_.Get(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
            auto width = static_cast<std::int32_t>(pixels.Width());
            auto height = static_cast<std::int32_t>(pixels.Height());
            for (std::uint32_t level = 1; level < mipLevels_; ++level) {
                Transition(commands, level - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
                const auto nextWidth = std::max(1, width / 2);
                const auto nextHeight = std::max(1, height / 2);
                const VkImageBlit blit{
                    .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 0, 1},
                    .srcOffsets = {{0, 0, 0}, {width, height, 1}},
                    .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1},
                    .dstOffsets = {{0, 0, 0}, {nextWidth, nextHeight, 1}}
                };
                vkCmdBlitImage(commands, image_.Get(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               image_.Get(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
                Transition(commands, level - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                width = nextWidth;
                height = nextHeight;
            }
            Transition(commands, mipLevels_ - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        });
        const VkImageViewCreateInfo viewInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = image_.Get(),
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = format,
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels_, 0, 1}
        };
        CheckVulkan(vkCreateImageView(uploads.Context().device, &viewInfo, nullptr, view_.Put()), "vkCreateImageView");
    }

    auto View() const noexcept -> VkImageView { return view_.Get(); }
    auto Levels() const noexcept -> std::uint32_t { return mipLevels_; }

private:
    static auto MipLevels(const VkPhysicalDevice device, const ImageData &pixels,
                          const VkFormat format) -> std::uint32_t {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(device, format, &properties);
        constexpr auto required = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
        if ((properties.optimalTilingFeatures & required) != required) {
            throw std::runtime_error{"Texture format does not support linear sampling"};
        }
        constexpr auto blit = VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT;
        return (properties.optimalTilingFeatures & blit) == blit
            ? std::bit_width(std::max(pixels.Width(), pixels.Height())) : 1u;
    }

    static auto ImageInfo(const ImageData &pixels, const VkFormat format,
                          const std::uint32_t levels) -> VkImageCreateInfo {
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

    auto Transition(const VkCommandBuffer commands, const std::uint32_t base, const std::uint32_t count,
                    const VkImageLayout oldLayout, const VkImageLayout newLayout) const -> void {
        const bool fromUndefined = oldLayout == VK_IMAGE_LAYOUT_UNDEFINED;
        const bool toShader = newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        const VkImageMemoryBarrier barrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = fromUndefined ? 0u : (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
                                                  ? VK_ACCESS_TRANSFER_READ_BIT : VK_ACCESS_TRANSFER_WRITE_BIT),
            .dstAccessMask = static_cast<VkAccessFlags>(toShader ? VK_ACCESS_SHADER_READ_BIT
                : (newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL ? VK_ACCESS_TRANSFER_READ_BIT : VK_ACCESS_TRANSFER_WRITE_BIT)),
            .oldLayout = oldLayout,
            .newLayout = newLayout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image_.Get(),
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, base, count, 0, 1}
        };
        vkCmdPipelineBarrier(commands,
                             fromUndefined ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_TRANSFER_BIT,
                             toShader ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    std::uint32_t mipLevels_;
    DeviceImage image_;
    ImageView view_;
};

} // namespace cg
