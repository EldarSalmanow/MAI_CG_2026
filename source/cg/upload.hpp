#pragma once

#include "vulkan_resources.hpp"

#include <limits>

namespace cg {

// Initialization-only transfers. Submit returns after the GPU has finished,
// so callers may immediately release their staging buffers.
class UploadContext final {
public:
    explicit UploadContext(const graphics::internal::Context &context)
        : context_{context}, pool_{context.device}, fence_{context.device} {
        const VkCommandPoolCreateInfo poolInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
            .queueFamilyIndex = context.graphics_queue_index
        };
        CheckVulkan(vkCreateCommandPool(context.device, &poolInfo, nullptr, pool_.Put()), "vkCreateCommandPool");
        const VkFenceCreateInfo fenceInfo{.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        CheckVulkan(vkCreateFence(context.device, &fenceInfo, nullptr, fence_.Put()), "vkCreateFence");
        const VkCommandBufferAllocateInfo allocation{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = pool_.Get(),
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1
        };
        CheckVulkan(vkAllocateCommandBuffers(context.device, &allocation, &commands_), "vkAllocateCommandBuffers");
    }

    template <class Record>
    auto Submit(Record &&record) const -> void {
        CheckVulkan(vkResetCommandPool(context_.device, pool_.Get(), 0), "vkResetCommandPool");
        const auto fence = fence_.Get();
        CheckVulkan(vkResetFences(context_.device, 1, &fence), "vkResetFences");
        const VkCommandBufferBeginInfo begin{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
        };
        CheckVulkan(vkBeginCommandBuffer(commands_, &begin), "vkBeginCommandBuffer");
        std::forward<Record>(record)(commands_);
        CheckVulkan(vkEndCommandBuffer(commands_), "vkEndCommandBuffer");
        const VkSubmitInfo submit{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1,
            .pCommandBuffers = &commands_
        };
        CheckVulkan(vkQueueSubmit(context_.graphics_queue, 1, &submit, fence), "vkQueueSubmit upload");
        CheckVulkan(vkWaitForFences(context_.device, 1, &fence, VK_TRUE,
                                  std::numeric_limits<std::uint64_t>::max()), "vkWaitForFences upload");
    }

    [[nodiscard]] auto UploadBuffer(const std::span<const std::byte> bytes,
                                    const VkBufferUsageFlags usage) const -> Buffer {
        if (bytes.empty()) {
            throw std::invalid_argument{"Cannot upload an empty mesh buffer"};
        }
        Buffer staging{context_.allocator, bytes.size_bytes(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT};
        staging.Upload(bytes);
        Buffer destination{context_.allocator, bytes.size_bytes(), usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                           MemoryLocation::Device};
        Submit([&](const VkCommandBuffer commands) {
            const VkBufferCopy copy{.size = bytes.size_bytes()};
            vkCmdCopyBuffer(commands, staging.Get(), destination.Get(), 1, &copy);
            const VkBufferMemoryBarrier barrier{
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = destination.Get(),
                .size = VK_WHOLE_SIZE
            };
            vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                                 0, 0, nullptr, 1, &barrier, 0, nullptr);
        });
        return destination;
    }

    auto Context() const noexcept -> const graphics::internal::Context & { return context_; }

private:
    const graphics::internal::Context &context_;
    CommandPool pool_;
    Fence fence_;
    VkCommandBuffer commands_{};
};

} // namespace cg
