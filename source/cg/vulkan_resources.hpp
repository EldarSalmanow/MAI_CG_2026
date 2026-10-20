#pragma once

#include "../graphics_internal.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cg {

    inline auto CheckVulkan(
        const VkResult result,
        const char *operation
    ) -> void {
        if (result != VK_SUCCESS) {
            throw std::runtime_error{
                std::string{operation} + " failed (VkResult " + std::to_string(result) + ")"
            };
        }
    }

    template <class Handle, auto Destroy>
    class DeviceHandle {
    public:
        DeviceHandle() = default;

        explicit DeviceHandle(
            const VkDevice device
        ) : device_{device} {}

        DeviceHandle(const DeviceHandle &other) = delete;

        DeviceHandle(DeviceHandle &&other) noexcept
                : device_{other.device_},
                  handle_{std::exchange(other.handle_, VK_NULL_HANDLE)} {}

    public:
        ~DeviceHandle() {
            Reset();
        }

    public:
        auto Get() const -> Handle {
            return handle_;
        }

        auto Put() -> Handle * {
            Reset();

            return &handle_;
        }

    private:
        auto Reset() noexcept -> void {
            if (handle_ != VK_NULL_HANDLE) {
                Destroy(device_, handle_, nullptr);

                handle_ = VK_NULL_HANDLE;
            }
        }

    public:
        auto operator=(const DeviceHandle &other) -> DeviceHandle & = delete;

        auto operator=(DeviceHandle &&other) noexcept -> DeviceHandle & {
            if (this == &other) {
                return *this;
            }

            Reset();

            device_ = other.device_;
            handle_ = std::exchange(other.handle_, VK_NULL_HANDLE);

            return *this;
        }

    private:
        VkDevice device_{VK_NULL_HANDLE};
        Handle handle_{VK_NULL_HANDLE};
    };

    using DescriptorSetLayout = DeviceHandle<VkDescriptorSetLayout, vkDestroyDescriptorSetLayout>;
    using DescriptorPool = DeviceHandle<VkDescriptorPool, vkDestroyDescriptorPool>;
    using PipelineLayout = DeviceHandle<VkPipelineLayout, vkDestroyPipelineLayout>;
    using Pipeline = DeviceHandle<VkPipeline, vkDestroyPipeline>;

    class ShaderModule {
    public:
        ShaderModule(
            const VkDevice device,
            const std::filesystem::path &path
        ) : handle_{device} {
            std::ifstream file{path, std::ios::binary | std::ios::ate};

            if (!file) {
                throw std::runtime_error{"Cannot open shader: " + path.string()};
            }

            const std::streamoff size = file.tellg();
            constexpr std::streamoff wordSize = sizeof(std::uint32_t);
            if (size < 5 * wordSize || size % wordSize != 0) {
                throw std::runtime_error{"Invalid SPIR-V size: " + path.string()};
            }

            std::vector<std::uint32_t> code(static_cast<std::size_t>(size / wordSize));
            file.seekg(0);
            if (!file.read(reinterpret_cast<char *>(code.data()), size)) {
                throw std::runtime_error{"Cannot read shader: " + path.string()};
            }

            if (code.front() != 0x07230203u) {
                throw std::runtime_error{"Invalid SPIR-V header: " + path.string()};
            }

            const VkShaderModuleCreateInfo info{
                .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                .codeSize = code.size() * sizeof(std::uint32_t),
                .pCode = code.data()
            };

            CheckVulkan(
                vkCreateShaderModule(device, &info, nullptr, handle_.Put()),
                "vkCreateShaderModule"
            );
        }

    public:
        auto Get() const -> VkShaderModule {
            return handle_.Get();
        }

    private:
        DeviceHandle<VkShaderModule, vkDestroyShaderModule> handle_;
    };

    class Buffer {
    public:
        Buffer() = default;

        Buffer(
            const VmaAllocator allocator,
            const VkDeviceSize size,
            const VkBufferUsageFlags usage
        ) : allocator_{allocator},
            size_{size} {
            const VkBufferCreateInfo info{
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .size = size,
                .usage = usage,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE
            };
            const VmaAllocationCreateInfo allocationInfo{
                .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, .usage = VMA_MEMORY_USAGE_AUTO
            };

            CheckVulkan(
                vmaCreateBuffer(allocator_, &info, &allocationInfo, &handle_, &allocation_, nullptr),
                "vmaCreateBuffer"
            );
        }

        Buffer(const Buffer &other) = delete;

        Buffer(Buffer &&other) noexcept {
            Swap(other);
        }

    public:
        ~Buffer() {
            Reset();
        }

    public:
        auto Get() const -> VkBuffer {
            return handle_;
        }

        auto Size() const -> VkDeviceSize {
            return size_;
        }

        auto Upload(
            const std::span<const std::byte> bytes
        ) const -> void {
            if (bytes.size_bytes() > size_) {
                throw std::out_of_range{"[ERROR] Upload exceeds buffer capacity!"};
            }

            if (!bytes.empty()) {
                CheckVulkan(
                    vmaCopyMemoryToAllocation(allocator_, bytes.data(), allocation_, 0, bytes.size_bytes()),
                    "vmaCopyMemoryToAllocation"
                );
            }
        }

        template <class T>
        void UploadValue(const T &value) const {
            Upload(std::as_bytes(std::span{&value, 1}));
        }

    private:
        auto Reset() noexcept -> void {
            if (handle_ != VK_NULL_HANDLE) {
                vmaDestroyBuffer(allocator_, handle_, allocation_);
            }
        }

        auto Swap(Buffer &other) noexcept -> void {
            std::swap(allocator_, other.allocator_);
            std::swap(handle_, other.handle_);
            std::swap(allocation_, other.allocation_);
            std::swap(size_, other.size_);
        }

    public:
        auto operator=(const Buffer &other) -> Buffer & = delete;

        auto operator=(Buffer &&other) noexcept -> Buffer & {
            if (this == &other) {
                return *this;
            }

            Buffer previous{std::move(other)};
            Swap(previous);

            return *this;
        }

    private:
        VmaAllocator allocator_{};
        VkBuffer handle_{VK_NULL_HANDLE};
        VmaAllocation allocation_{};
        VkDeviceSize size_{0};
    };

    template <class T>
    class StorageBuffer {
    public:
        StorageBuffer(
            const VmaAllocator allocator,
            const VkDeviceSize maxBytes
        ) : allocator_{allocator},
            limit_{maxBytes / sizeof(T)} {
            Upload({});
        }

    public:
        auto Upload(
            const std::span<const T> values
        ) -> bool {
            const auto required = std::max(std::size_t{1}, values.size());

            if (required > limit_) {
                throw std::length_error{"Light array exceeds maxStorageBufferRange"};
            }

            const bool resized = required > capacity_;

            if (resized) {
                const auto capacity = std::max(required,
                                                              std::min(limit_,
                                                                       std::max(std::size_t{1}, capacity_ * 2)));
                buffer_ = Buffer{allocator_, capacity * sizeof(T), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT};
                capacity_ = capacity;
            }

            buffer_.Upload(std::as_bytes(values));

            return resized;
        }

        auto GetBuffer() const -> const Buffer & {
            return buffer_;
        }

        auto Limit() const -> std::size_t {
            return limit_;
        }

    private:
        VmaAllocator allocator_;
        std::size_t limit_;
        std::size_t capacity_ = 0;
        Buffer buffer_;
    };

} // namespace cg
