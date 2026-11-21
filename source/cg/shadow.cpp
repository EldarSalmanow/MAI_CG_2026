#include "shadow.hpp"

#include <array>

namespace cg {

    ShadowMap::ShadowMap(
        const graphics::internal::Context &context
    )
        : format_{DepthFormat(context.physical_device)},
          image_{context.allocator, ImageInfo(format_)},
          view_{context.device},
          sampler_{context.device} {
        const VkImageViewCreateInfo viewInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = image_.Get(),
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = format_,
            .subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1}
        };
        CheckVulkan(vkCreateImageView(context.device, &viewInfo, nullptr, view_.Put()), "vkCreateImageView shadow");

        const VkSamplerCreateInfo samplerInfo{
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = VK_FILTER_NEAREST,
            .minFilter = VK_FILTER_NEAREST,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
            .compareEnable = VK_TRUE,
            .compareOp = VK_COMPARE_OP_LESS,
            .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE
        };
        CheckVulkan(vkCreateSampler(context.device, &samplerInfo, nullptr, sampler_.Put()), "vkCreateSampler shadow");
    }

    auto ShadowMap::DepthFormat(
        const VkPhysicalDevice device
    ) -> VkFormat {
        constexpr std::array candidates{VK_FORMAT_D32_SFLOAT, VK_FORMAT_D16_UNORM};
        constexpr auto required = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;

        for (const auto format : candidates) {
            VkFormatProperties properties{};
            vkGetPhysicalDeviceFormatProperties(device, format, &properties);
            if ((properties.optimalTilingFeatures & required) == required) {
                return format;
            }
        }

        throw std::runtime_error{"[ERROR] No sampled depth attachment format is supported!"};
    }

    auto ShadowMap::ImageInfo(
        const VkFormat format
    ) -> VkImageCreateInfo {
        return {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = format,
            .extent = {resolution, resolution, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
        };
    }

    auto ShadowMap::Begin(
        const VkCommandBuffer commands
    ) -> void {
        const VkImageMemoryBarrier barrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = initialized_ ? static_cast<VkAccessFlags>(VK_ACCESS_SHADER_READ_BIT) : 0u,
            .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .oldLayout = initialized_ ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image_.Get(),
            .subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1}
        };
        const auto sourceStage =
            initialized_ ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        constexpr auto depthStages =
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        vkCmdPipelineBarrier(commands, sourceStage, depthStages, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        const VkRenderingAttachmentInfo depth{
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = view_.Get(),
            .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = {.depthStencil = {.depth = 1.0f}}
        };
        const VkRenderingInfo rendering{
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = {.extent = {resolution, resolution}},
            .layerCount = 1,
            .pDepthAttachment = &depth
        };
        vkCmdBeginRendering(commands, &rendering);

        const VkViewport viewport{
            .width = static_cast<float>(resolution), .height = static_cast<float>(resolution), .maxDepth = 1.0f
        };
        const VkRect2D scissor{.extent = {resolution, resolution}};
        vkCmdSetViewport(commands, 0, 1, &viewport);
        vkCmdSetScissor(commands, 0, 1, &scissor);
    }

    auto ShadowMap::End(
        const VkCommandBuffer commands
    ) -> void {
        vkCmdEndRendering(commands);

        const VkImageMemoryBarrier barrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image_.Get(),
            .subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1}
        };
        constexpr auto depthStages =
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        vkCmdPipelineBarrier(
            commands, depthStages, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier
        );

        initialized_ = true;
    }

    auto ShadowMap::Descriptor() const -> VkDescriptorImageInfo {
        return {sampler_.Get(), view_.Get(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    }

    auto ShadowMap::Format() const -> VkFormat {
        return format_;
    }

} // namespace cg
