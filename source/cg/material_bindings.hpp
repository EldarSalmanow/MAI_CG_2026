#pragma once

#include "gpu_data.hpp"
#include "texture.hpp"

#include <array>
#include <bit>
#include <cstring>
#include <limits>
#include <map>
#include <memory>

namespace cg {

class ModelBuffer final {
public:
    explicit ModelBuffer(const graphics::internal::Context &context) : allocator_{context.allocator} {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(context.physical_device, &properties);
        if (sizeof(ModelUniform) > properties.limits.maxUniformBufferRange) {
            throw std::runtime_error{"ModelUniform exceeds maxUniformBufferRange"};
        }
        const auto alignment = std::max(VkDeviceSize{1}, properties.limits.minUniformBufferOffsetAlignment);
        stride_ = (sizeof(ModelUniform) + alignment - 1) / alignment * alignment;
        EnsureCapacity(4);
    }

    auto EnsureCapacity(const std::size_t count) -> bool {
        if (count <= capacity_) {
            return false;
        }
        const auto limit = std::numeric_limits<std::uint32_t>::max() / stride_;
        if (count > limit) {
            throw std::length_error{"Model count exceeds Vulkan dynamic offset range"};
        }
        const auto capacity = std::min(VkDeviceSize{std::bit_ceil(count)}, limit);
        Buffer replacement{allocator_, capacity * stride_, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT};
        data_.resize(static_cast<std::size_t>(capacity * stride_));
        buffer_ = std::move(replacement);
        capacity_ = static_cast<std::size_t>(capacity);
        return true;
    }

    auto Write(const std::size_t index, const ModelUniform &model) -> void {
        if (index >= capacity_) {
            throw std::out_of_range{"Model buffer index exceeds capacity"};
        }
        std::memcpy(data_.data() + index * stride_, &model, sizeof(model));
    }

    auto Flush(const std::size_t count) const -> void {
        if (count > capacity_) {
            throw std::out_of_range{"Model upload exceeds capacity"};
        }
        buffer_.Upload(std::span{data_}.first(static_cast<std::size_t>(count * stride_)));
    }

    auto Offset(const std::size_t index) const -> std::uint32_t {
        if (index >= capacity_) {
            throw std::out_of_range{"Invalid model dynamic offset"};
        }
        return static_cast<std::uint32_t>(index * stride_);
    }

    auto Get() const noexcept -> VkBuffer { return buffer_.Get(); }
    auto Stride() const noexcept -> VkDeviceSize { return stride_; }

private:
    VmaAllocator allocator_;
    VkDeviceSize stride_{};
    std::size_t capacity_{};
    Buffer buffer_;
    std::vector<std::byte> data_;
};

class MaterialBindings final {
public:
    MaterialBindings(const UploadContext &uploads, const VkDescriptorSetLayout layout,
                     const std::filesystem::path &directory)
        : device_{uploads.Context().device}, models_{uploads.Context()}, sampler_{device_}, pool_{device_} {
        const VkSamplerCreateInfo samplerInfo{
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = VK_FILTER_LINEAR,
            .minFilter = VK_FILTER_LINEAR,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .maxLod = VK_LOD_CLAMP_NONE
        };
        CheckVulkan(vkCreateSampler(device_, &samplerInfo, nullptr, sampler_.Put()), "vkCreateSampler");
        constexpr auto count = static_cast<std::uint32_t>(MaterialDefinitions.size());
        constexpr std::array sizes{
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, count},
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, count * 3}
        };
        const VkDescriptorPoolCreateInfo poolInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = count,
            .poolSizeCount = static_cast<std::uint32_t>(sizes.size()),
            .pPoolSizes = sizes.data()
        };
        CheckVulkan(vkCreateDescriptorPool(device_, &poolInfo, nullptr, pool_.Put()), "vkCreateDescriptorPool materials");
        std::array<VkDescriptorSetLayout, count> layouts;
        layouts.fill(layout);
        const VkDescriptorSetAllocateInfo allocation{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = pool_.Get(),
            .descriptorSetCount = count,
            .pSetLayouts = layouts.data()
        };
        CheckVulkan(vkAllocateDescriptorSets(device_, &allocation, sets_.data()), "vkAllocateDescriptorSets materials");

        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(uploads.Context().physical_device, &properties);
        const auto load = [&](const std::string_view filename, const VkFormat format) -> VkImageView {
            const auto path = directory / (filename.empty() ? "black.png" : filename);
            const auto key = std::pair{path, format};
            auto found = textures_.find(key);
            if (found == textures_.end()) {
                const ImageData image{path, properties.limits.maxImageDimension2D};
                found = textures_.emplace(key, std::make_unique<Texture>(uploads, image, format)).first;
            }
            return found->second->View();
        };
        for (std::size_t i = 0; i < MaterialDefinitions.size(); ++i) {
            const auto &definition = MaterialDefinitions[i];
            const std::array views{
                load(definition.albedo, VK_FORMAT_R8G8B8A8_SRGB),
                load(definition.specular, VK_FORMAT_R8G8B8A8_UNORM),
                load(definition.emissive, VK_FORMAT_R8G8B8A8_SRGB)
            };
            for (std::uint32_t map = 0; map < views.size(); ++map) {
                const VkDescriptorImageInfo imageInfo{sampler_.Get(), views[map], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
                const VkWriteDescriptorSet write{
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = sets_[i],
                    .dstBinding = map + 1,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .pImageInfo = &imageInfo
                };
                vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
            }
        }
        UpdateModelDescriptors();
    }

    auto Prepare(const std::size_t count) -> void {
        if (models_.EnsureCapacity(count)) {
            UpdateModelDescriptors();
        }
    }

    auto Write(const std::size_t index, const ModelUniform &model) -> void { models_.Write(index, model); }
    auto Flush(const std::size_t count) const -> void { models_.Flush(count); }

    auto Bind(const VkCommandBuffer commands, const VkPipelineLayout layout,
              const MaterialPreset material, const std::size_t index) const -> void {
        const auto offset = models_.Offset(index);
        const auto set = sets_.at(static_cast<std::size_t>(material));
        vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 1, 1, &set, 1, &offset);
    }

private:
    auto UpdateModelDescriptors() const -> void {
        const VkDescriptorBufferInfo bufferInfo{models_.Get(), 0, sizeof(ModelUniform)};
        for (const auto set : sets_) {
            const VkWriteDescriptorSet write{
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = set,
                .dstBinding = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                .pBufferInfo = &bufferInfo
            };
            vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
        }
    }

    VkDevice device_;
    ModelBuffer models_;
    Sampler sampler_;
    std::map<std::pair<std::filesystem::path, VkFormat>, std::unique_ptr<Texture>> textures_;
    DescriptorPool pool_;
    std::array<VkDescriptorSet, MaterialDefinitions.size()> sets_{};
};

} // namespace cg
