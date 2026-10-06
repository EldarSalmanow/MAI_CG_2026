#include "application.hpp"
#include "graphics_internal.hpp"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <imgui.h>
#include <vk_mem_alloc.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numbers>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
    struct Vertex {
        glm::vec3 pos;
        glm::vec3 color;
    };

    constexpr auto vertices = [] {
        constexpr std::array<glm::vec3, 8> positions {
            {
                {-1, -1, -1},
                {1, -1, -1},
                {1, 1, -1},
                {-1, 1, -1},
                {-1, -1, 1},
                {1, -1, 1},
                {1, 1, 1},
                {-1, 1, 1}
            }
        };

        std::array<Vertex, positions.size()> result{};
        for (std::size_t i = 0; i < positions.size(); ++i) {
            result[i] = {
                .pos = positions[i],
                .color = (positions[i] + glm::vec3{1}) * 0.5f
            };
        }

        return result;
    }();

    constexpr std::array<std::uint16_t, 36> indices = {
        0, 1, 2, 2, 3, 0, // Передняя
        1, 5, 6, 6, 2, 1, // Правая
        7, 6, 5, 5, 4, 7, // Задняя
        4, 0, 3, 3, 7, 4, // Левая
        4, 5, 1, 1, 0, 4, // Нижняя
        3, 2, 6, 6, 7, 3  // Верхняя
    };

    struct UniformBufferObject {
        glm::mat4 model;
        glm::mat4 view;
        glm::mat4 proj;
        glm::vec4 color;
    };

    struct Buffer {
        VkBuffer handle{};
        VmaAllocation allocation{};
    };

    struct Animation {
        bool enabled = false;
        bool playing = false;
        double time = 0;
        float speed = 1;
        float radius = 1;

        void advance(const double dt) {
            if (enabled && playing) {
                time += dt * speed;
            }
        }
    };

    struct Object {
        glm::vec3 position{0};
        glm::vec3 rotation{0};
        glm::vec3 scale{1};
        glm::vec3 color{1};
        Animation animation;
    };

    std::array<Object, 2> objects {
        Object{
            .position = {0, 0, 0},
            .color = {0.5f, 0.5f, 1}
        },
        Object{
            .position = {-4, 0, -2},
            .color = {0.5f, 0.5f, 1}
        }
    };
    constexpr auto objectCount = static_cast<std::uint32_t>(objects.size());
    Buffer vertexBuffer, indexBuffer;
    std::array<Buffer, objectCount> ubos{};
    VkDescriptorSetLayout descriptorSetLayout{};
    VkDescriptorPool descriptorPool{};
    std::array<VkDescriptorSet, objectCount> descriptorSets{};
    VkPipelineLayout pipelineLayout{};
    VkPipeline graphicsPipeline{};
    int projectionType = 0;
    int selectedObject = 0;
    std::optional<double> lastTime;

    auto Check(
        const VkResult result,
        const char *operation
    ) -> void {
        if (result != VK_SUCCESS) {
            std::stringstream stream;

            stream << "[ERROR] " << operation << " failed: " << std::to_string(result);

            throw std::runtime_error(stream.str());
        }
    }

    class Shader {
    public:
        explicit Shader(const char *name) {
            const auto path = std::filesystem::path {CG_SHADER_DIR} / name;

            std::ifstream file {
                path,
                std::ios::binary | std::ios::ate
            };

            if (!file) {
                throw std::runtime_error("[ERROR] Can`t open shader: " + path.string());
            }

            const auto size = file.tellg();

            if (size <= 0 || size % sizeof(std::uint32_t) != 0) {
                throw std::runtime_error("[ERROR] Invalid SPIR-V size: " + path.string());
            }

            std::vector<std::uint32_t> code(static_cast<std::size_t>(size) / sizeof(std::uint32_t));

            file.seekg(0);

            if (!file.read(reinterpret_cast<char *>(code.data()), size)) {
                throw std::runtime_error("[ERROR] Can`t read shader: " + path.string());
            }

            const VkShaderModuleCreateInfo info{
                .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                .codeSize = static_cast<std::size_t>(size),
                .pCode = code.data()
            };

            Check(
                vkCreateShaderModule(graphics::internal::context.device, &info, nullptr, &handle),
                "vkCreateShaderModule"
            );
        }

        Shader(const Shader &other) = delete;

    public:
        ~Shader() {
            vkDestroyShaderModule(graphics::internal::context.device, handle, nullptr);
        }

    public:
        auto operator=(const Shader &other) -> Shader & = delete;

    public:
        VkShaderModule handle{};
    };

    auto CreateBuffer(
        const VkDeviceSize size,
        const VkBufferUsageFlags usage
    ) -> Buffer {
        const VkBufferCreateInfo buffer_info{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = size,
            .usage = usage
        };
        const VmaAllocationCreateInfo allocation_info{
            .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
            .usage = VMA_MEMORY_USAGE_AUTO
        };

        Buffer buffer;
        Check(
            vmaCreateBuffer(graphics::internal::context.allocator, &buffer_info, &allocation_info, &buffer.handle, &buffer.allocation, nullptr),
            "vmaCreateBuffer"
        );

        return buffer;
    }

    auto Upload(
        const Buffer &buffer,
        std::span<const std::byte> data
    ) -> void {
        Check(
            vmaCopyMemoryToAllocation(graphics::internal::context.allocator, data.data(), buffer.allocation, 0, data.size_bytes()),
            "vmaCopyMemoryToAllocation"
        );
    }

    auto ModelMatrix(
        const Object &object
    ) -> glm::mat4 {
        auto position = object.position;
        auto rotation = glm::radians(object.rotation);

        if (object.animation.enabled) {
            const auto t = object.animation.time;
            const auto s = std::sin(t);
            const auto x = object.animation.radius * std::cos(t) / (1 + s * s);

            position += glm::vec3{static_cast<float>(x), 0, static_cast<float>(x * s)};

            rotation.y += static_cast<float>(std::fmod(2 * t, 2 * std::numbers::pi));
        }

        auto model = glm::translate(glm::mat4{1}, position);
        model = glm::rotate(model, rotation.x, glm::vec3{1, 0, 0});
        model = glm::rotate(model, rotation.y, glm::vec3{0, 1, 0});
        model = glm::rotate(model, rotation.z, glm::vec3{0, 0, 1});

        return glm::scale(model, object.scale);
    }

    void DrawControls() {
        if (ImGui::Begin("Laboratory Work 1")) {
            ImGui::RadioButton("Perspective", &projectionType, 0);
            ImGui::SameLine();
            ImGui::RadioButton("Orthographic", &projectionType, 1);

            ImGui::Separator();

            ImGui::Combo("Object", &selectedObject, "Cube 1\0Cube 2\0");

            auto &object = objects[static_cast<std::size_t>(selectedObject)];
            ImGui::ColorEdit3("Color", glm::value_ptr(object.color));
            ImGui::SliderFloat3("Position", glm::value_ptr(object.position), -5, 5);
            ImGui::SliderFloat3("Rotation", glm::value_ptr(object.rotation), 0, 360);
            ImGui::SliderFloat3("Scale", glm::value_ptr(object.scale), 0.1f, 3);

            ImGui::Separator();

            auto &animation = object.animation;
            ImGui::Checkbox("Lemniscate animation", &animation.enabled);
            ImGui::BeginDisabled(!animation.enabled);
            if (ImGui::Button(animation.playing ? "Pause" : "Play")) {
                animation.playing = !animation.playing;
            }
            ImGui::SliderFloat("Speed", &animation.speed, 0.1f, 5);
            ImGui::SliderFloat("Radius", &animation.radius, 1, 10);
            ImGui::EndDisabled();
        }

        ImGui::End();
    }

} // namespace

namespace application {

auto Initialize() -> bool try {
    auto &context = graphics::internal::context;

    vertexBuffer = CreateBuffer(sizeof(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    Upload(vertexBuffer, std::as_bytes(std::span{vertices}));

    indexBuffer = CreateBuffer(sizeof(indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    Upload(indexBuffer, std::as_bytes(std::span{indices}));

    for (auto &ubo : ubos)
        ubo = CreateBuffer(sizeof(UniformBufferObject), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

    VkDescriptorSetLayoutBinding uboLayoutBinding {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT
    };
    VkDescriptorSetLayoutCreateInfo layoutInfo {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &uboLayoutBinding
    };
    Check(
        vkCreateDescriptorSetLayout(context.device, &layoutInfo, nullptr, &descriptorSetLayout),
        "vkCreateDescriptorSetLayout"
    );

    VkDescriptorPoolSize poolSize {
        .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = objectCount
    };
    VkDescriptorPoolCreateInfo poolInfo {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = objectCount,
        .poolSizeCount = 1,
        .pPoolSizes = &poolSize
    };
    Check(
        vkCreateDescriptorPool(context.device, &poolInfo, nullptr, &descriptorPool),
        "vkCreateDescriptorPool"
    );

    std::array<VkDescriptorSetLayout, objectCount> layouts {};
    layouts.fill(descriptorSetLayout);

    VkDescriptorSetAllocateInfo allocInfo {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptorPool,
        .descriptorSetCount = objectCount,
        .pSetLayouts = layouts.data()
    };
    Check(
        vkAllocateDescriptorSets(context.device, &allocInfo, descriptorSets.data()),
        "vkAllocateDescriptorSets"
    );

    for (std::size_t i = 0; i < objects.size(); ++i) {
        VkDescriptorBufferInfo bInfo {
            .buffer = ubos[i].handle,
            .offset = 0,
            .range = sizeof(UniformBufferObject)
        };
        VkWriteDescriptorSet descriptorWrite{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptorSets[i],
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo = &bInfo
        };
        vkUpdateDescriptorSets(context.device, 1, &descriptorWrite, 0, nullptr);
    }

    const Shader vertex_shader{"shader.vert.spv"};
    const Shader fragment_shader{"shader.frag.spv"};

    VkPipelineShaderStageCreateInfo shaderStages[] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vertex_shader.handle,
            .pName = "main"
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fragment_shader.handle,
            .pName = "main"
        }
    };

    VkVertexInputBindingDescription bindingDescription {
        .binding = 0,
        .stride = sizeof(Vertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
    };
    std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions = {
        VkVertexInputAttributeDescription{
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(Vertex, pos)
        },
        VkVertexInputAttributeDescription{
            .location = 1,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(Vertex, color)
        }
    };

    VkPipelineVertexInputStateCreateInfo vertexInputInfo {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &bindingDescription,
        .vertexAttributeDescriptionCount = 2,
        .pVertexAttributeDescriptions = attributeDescriptions.data()
    };
    VkPipelineInputAssemblyStateCreateInfo inputAssembly {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
    };

    VkPipelineViewportStateCreateInfo viewportState {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1
    };

    VkPipelineRasterizationStateCreateInfo rasterizer {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
        .lineWidth = 1.0f
    };
    VkPipelineMultisampleStateCreateInfo multisampling {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
    };

    VkPipelineDepthStencilStateCreateInfo depthStencil {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS
    };

    VkPipelineColorBlendAttachmentState colorBlendAttachment {
        .blendEnable = VK_FALSE,
        .colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT
          | VK_COLOR_COMPONENT_G_BIT
          | VK_COLOR_COMPONENT_B_BIT
          | VK_COLOR_COMPONENT_A_BIT
    };
    VkPipelineColorBlendStateCreateInfo colorBlending {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &colorBlendAttachment
    };

    constexpr std::array dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicState {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data()
    };

    VkPipelineLayoutCreateInfo pipelineLayoutInfo {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &descriptorSetLayout
    };
    Check(
        vkCreatePipelineLayout(context.device, &pipelineLayoutInfo, nullptr, &pipelineLayout),
        "vkCreatePipelineLayout"
    );

    VkGraphicsPipelineCreateInfo pipelineInfo {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2,
        .pStages = shaderStages,
        .pVertexInputState = &vertexInputInfo,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = &depthStencil,
        .pColorBlendState = &colorBlending,
        .pDynamicState = &dynamicState,
        .layout = pipelineLayout,
        .renderPass = context.render_pass,
        .subpass = 0
    };
    Check(
        vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline),
        "vkCreateGraphicsPipelines"
    );

    return true;
} catch (const std::exception &exception) {
    std::cerr << "[ERROR] Application initialization failed: " << exception.what() << std::endl;

    Shutdown();

    return false;
}

auto Shutdown() -> void {
    const auto &context = graphics::internal::context;

    vkDeviceWaitIdle(context.device);

    vkDestroyPipeline(context.device, graphicsPipeline, nullptr);
    vkDestroyPipelineLayout(context.device, pipelineLayout, nullptr);
    vkDestroyDescriptorPool(context.device, descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(context.device, descriptorSetLayout, nullptr);
    for (auto &ubo : ubos) {
        vmaDestroyBuffer(context.allocator, ubo.handle, ubo.allocation);
        ubo = {};
    }
    vmaDestroyBuffer(context.allocator, indexBuffer.handle, indexBuffer.allocation);
    vmaDestroyBuffer(context.allocator, vertexBuffer.handle, vertexBuffer.allocation);
    indexBuffer = {};
    vertexBuffer = {};
    graphicsPipeline = VK_NULL_HANDLE;
    pipelineLayout = VK_NULL_HANDLE;
    descriptorPool = VK_NULL_HANDLE;
    descriptorSetLayout = VK_NULL_HANDLE;
    descriptorSets.fill(VK_NULL_HANDLE);
    lastTime.reset();
}

auto Update(
    double time
) -> void {
    const auto dt =
        lastTime ? time - *lastTime
                 : 0;

    lastTime = time;

    DrawControls();

    const auto &context = graphics::internal::context;
    const auto aspect =
        static_cast<float>(context.swapchain_extent.width)
      / static_cast<float>(context.swapchain_extent.height);
    const auto view = glm::lookAt(glm::vec3{0, 3, 10}, glm::vec3{0}, glm::vec3{0, 1, 0});
    auto projection =
        projectionType == 0 ? glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f)
                            : glm::ortho(-5.0f * aspect, 5.0f * aspect, -5.0f, 5.0f, 0.1f, 100.0f);
    projection[1][1] *= -1;

    for (std::size_t i = 0; i < objects.size(); ++i) {
        auto &object = objects[i];

        object.animation.advance(dt);

        const UniformBufferObject data {
            .model = ModelMatrix(object),
            .view = view,
            .proj = projection,
            .color = glm::vec4{object.color, 1}
        };

        Upload(ubos[i], std::as_bytes(std::span{&data, 1}));
    }
}

auto Render(
    const graphics::internal::FrameData &frame_data
) -> void {
    const auto &context = graphics::internal::context;
    const auto command_buffer = frame_data.command_buffer;

    constexpr VkCommandBufferBeginInfo begin_info {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };

    Check(
        vkBeginCommandBuffer(command_buffer, &begin_info),
        "vkBeginCommandBuffer"
    );

    constexpr VkClearValue clear_values[] = {
        {
            .color = {
                .float32 = {0.15f, 0.15f, 0.15f, 1.0f}
            }
        },
        {
            .depthStencil = {
                .depth = 1.0f, .stencil = 0
            }
        }
    };
    const VkRenderPassBeginInfo render_pass_begin_info {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = context.render_pass,
        .framebuffer = frame_data.framebuffer,
        .renderArea = {
            .offset = {0, 0},
            .extent = context.swapchain_extent
        },
        .clearValueCount = 2,
        .pClearValues = clear_values
    };

    vkCmdBeginRenderPass(command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

    const VkViewport viewport{
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<float>(context.swapchain_extent.width),
        .height = static_cast<float>(context.swapchain_extent.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f
    };
    vkCmdSetViewport(command_buffer, 0, 1, &viewport);

    const VkRect2D scissor {
        .offset = {0, 0},
        .extent = context.swapchain_extent
    };
    vkCmdSetScissor(command_buffer, 0, 1, &scissor);

    constexpr VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(command_buffer, 0, 1, &vertexBuffer.handle, offsets);
    vkCmdBindIndexBuffer(command_buffer, indexBuffer.handle, 0, VK_INDEX_TYPE_UINT16);

    for (const auto &descriptor_set : descriptorSets) {
        vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptor_set, 0, nullptr);

        vkCmdDrawIndexed(command_buffer, indices.size(), 1, 0, 0, 0);
    }

    vkCmdEndRenderPass(command_buffer);
    Check(
        vkEndCommandBuffer(command_buffer),
        "vkEndCommandBuffer"
    );
}

} // namespace application