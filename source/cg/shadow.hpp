#pragma once

#include "vulkan_resources.hpp"

namespace cg {

    class ShadowMap final {
    public:
        explicit ShadowMap(
            const graphics::internal::Context &context
        );

    public:
        auto Begin(
            VkCommandBuffer commands
        ) -> void;

        auto End(
            VkCommandBuffer commands
        ) -> void;

        auto Descriptor() const -> VkDescriptorImageInfo;

        auto Format() const -> VkFormat;

    private:
        static auto DepthFormat(
            VkPhysicalDevice device
        ) -> VkFormat;

        static auto ImageInfo(
            VkFormat format
        ) -> VkImageCreateInfo;

        static constexpr std::uint32_t resolution = 2048;

        VkFormat format_;
        DeviceImage image_;
        ImageView view_;
        Sampler sampler_;
        bool initialized_{false};
    };

} // namespace cg
