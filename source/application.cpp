#include "application.hpp"
#include "graphics_internal.hpp"

// ВАЖНО: Исправляет баги с обрезкой геометрии в Vulkan!
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <imgui.h>
#include <vk_mem_alloc.h>

#include <vector>
#include <fstream>
#include <iostream>
#include <array>

// --- Структуры данных ---
struct Vertex {
    glm::vec3 pos;
    glm::vec3 color;
};

// Задание 5: Процедурные цвета для вершин куба
const std::vector<Vertex> vertices = {
    {{-1.0f, -1.0f, -1.0f}, {0.0f, 0.0f, 0.0f}},
    {{ 1.0f, -1.0f, -1.0f}, {1.0f, 0.0f, 0.0f}},
    {{ 1.0f,  1.0f, -1.0f}, {1.0f, 1.0f, 0.0f}},
    {{-1.0f,  1.0f, -1.0f}, {0.0f, 1.0f, 0.0f}},
    {{-1.0f, -1.0f,  1.0f}, {0.0f, 0.0f, 1.0f}},
    {{ 1.0f, -1.0f,  1.0f}, {1.0f, 0.0f, 1.0f}},
    {{ 1.0f,  1.0f,  1.0f}, {1.0f, 1.0f, 1.0f}},
    {{-1.0f,  1.0f,  1.0f}, {0.0f, 1.0f, 1.0f}}
};

const std::vector<uint16_t> indices = {
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
    glm::vec4 uiColor;
};

namespace {
    // --- Состояние ресурсов Vulkan ---
    VkBuffer vertexBuffer; VmaAllocation vertexBufferAlloc;
    VkBuffer indexBuffer;  VmaAllocation indexBufferAlloc;

    std::array<VkBuffer, 2> ubos;
    std::array<VmaAllocation, 2> uboAllocs;

    VkDescriptorSetLayout descriptorSetLayout;
    VkDescriptorPool descriptorPool;
    std::array<VkDescriptorSet, 2> descriptorSets;

    VkPipelineLayout pipelineLayout;
    VkPipeline graphicsPipeline;

    // --- Состояние интерфейса ---
    int projectionType = 0;
    glm::vec3 objPosition{0.0f, 0.0f, 0.0f};
    glm::vec3 objRotation{0.0f, 0.0f, 0.0f};
    glm::vec3 objScale{1.0f, 1.0f, 1.0f};
    glm::vec3 guiColor{1.0f, 1.0f, 1.0f};

    bool isAnimating = false;
    float animSpeed = 2.0f;
    float animRadius = 3.0f;
    float animTime = 0.0f;
    double lastTime = 0.0;

    // Утилиты
    std::vector<char> readFile(const std::string& filename) {
        std::ifstream file(filename, std::ios::ate | std::ios::binary);
        if (!file.is_open()) throw std::runtime_error("Не удалось открыть файл " + filename);
        size_t fileSize = (size_t)file.tellg();
        std::vector<char> buffer(fileSize);
        file.seekg(0); file.read(buffer.data(), fileSize); file.close();
        return buffer;
    }

    VkShaderModule createShaderModule(const std::vector<char>& code) {
        VkShaderModuleCreateInfo createInfo{.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = code.size(), .pCode = reinterpret_cast<const uint32_t*>(code.data())};
        VkShaderModule shaderModule;
        vkCreateShaderModule(graphics::internal::context.device, &createInfo, nullptr, &shaderModule);
        return shaderModule;
    }

    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& buffer, VmaAllocation& allocation) {
        VkBufferCreateInfo bufferInfo{.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size, .usage = usage};
        VmaAllocationCreateInfo allocInfo{.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, .usage = VMA_MEMORY_USAGE_AUTO};
        vmaCreateBuffer(graphics::internal::context.allocator, &bufferInfo, &allocInfo, &buffer, &allocation, nullptr);
    }
} // namespace

namespace application {

auto Initialize() -> bool {
    auto& ctx = graphics::internal::context;

    // 1. Буферы геометрии
    createBuffer(sizeof(vertices[0]) * vertices.size(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertexBuffer, vertexBufferAlloc);
    void* data;
    vmaMapMemory(ctx.allocator, vertexBufferAlloc, &data);
    memcpy(data, vertices.data(), sizeof(vertices[0]) * vertices.size());
    vmaUnmapMemory(ctx.allocator, vertexBufferAlloc);

    createBuffer(sizeof(indices[0]) * indices.size(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, indexBuffer, indexBufferAlloc);
    vmaMapMemory(ctx.allocator, indexBufferAlloc, &data);
    memcpy(data, indices.data(), sizeof(indices[0]) * indices.size());
    vmaUnmapMemory(ctx.allocator, indexBufferAlloc);

    // 2. Uniform Buffers (по одному на объект)
    for(int i = 0; i < 2; i++) {
        createBuffer(sizeof(UniformBufferObject), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ubos[i], uboAllocs[i]);
    }

    // 3. Дескрипторы (Задание 6: 2 сета из 1 пула)
    VkDescriptorSetLayoutBinding uboLayoutBinding{.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_VERTEX_BIT};
    VkDescriptorSetLayoutCreateInfo layoutInfo{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 1, .pBindings = &uboLayoutBinding};
    vkCreateDescriptorSetLayout(ctx.device, &layoutInfo, nullptr, &descriptorSetLayout);

    VkDescriptorPoolSize poolSize{.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 2};
    VkDescriptorPoolCreateInfo poolInfo{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 2, .poolSizeCount = 1, .pPoolSizes = &poolSize};
    vkCreateDescriptorPool(ctx.device, &poolInfo, nullptr, &descriptorPool);

    std::array<VkDescriptorSetLayout, 2> layouts = {descriptorSetLayout, descriptorSetLayout};
    VkDescriptorSetAllocateInfo allocInfo{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = descriptorPool, .descriptorSetCount = 2, .pSetLayouts = layouts.data()};
    vkAllocateDescriptorSets(ctx.device, &allocInfo, descriptorSets.data());

    for(int i = 0; i < 2; i++) {
        VkDescriptorBufferInfo bInfo{.buffer = ubos[i], .offset = 0, .range = sizeof(UniformBufferObject)};
        VkWriteDescriptorSet descriptorWrite{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = descriptorSets[i], .dstBinding = 0, .dstArrayElement = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .pBufferInfo = &bInfo};
        vkUpdateDescriptorSets(ctx.device, 1, &descriptorWrite, 0, nullptr);
    }

    // 4. Пайплайн
    auto vertMod = createShaderModule(readFile("shader.vert.spv"));
    auto fragMod = createShaderModule(readFile("shader.frag.spv"));

    VkPipelineShaderStageCreateInfo shaderStages[] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vertMod, .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fragMod, .pName = "main"}
    };

    VkVertexInputBindingDescription bindingDescription{.binding = 0, .stride = sizeof(Vertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};
    std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions = {
        VkVertexInputAttributeDescription{.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, pos)},
        VkVertexInputAttributeDescription{.location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, color)}
    };

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &bindingDescription, .vertexAttributeDescriptionCount = 2, .pVertexAttributeDescriptions = attributeDescriptions.data()};
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};

    VkPipelineViewportStateCreateInfo viewportState{.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1};

    // ИСПРАВЛЕНИЕ CULLING: Так как мы инвертируем Y, CCW становится CW. Поэтому frontFace = CLOCKWISE
    VkPipelineRasterizationStateCreateInfo rasterizer{.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_BACK_BIT, .frontFace = VK_FRONT_FACE_CLOCKWISE, .lineWidth = 1.0f};
    VkPipelineMultisampleStateCreateInfo multisampling{.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};

    // ИСПРАВЛЕНИЕ Z-BUFFER
    VkPipelineDepthStencilStateCreateInfo depthStencil{.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, .depthTestEnable = VK_TRUE, .depthWriteEnable = VK_TRUE, .depthCompareOp = VK_COMPARE_OP_LESS};

    VkPipelineColorBlendAttachmentState colorBlendAttachment{.blendEnable = VK_FALSE, .colorWriteMask = 0xF}; // 0xF = R|G|B|A
    VkPipelineColorBlendStateCreateInfo colorBlending{.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1, .pAttachments = &colorBlendAttachment};

    std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, .dynamicStateCount = (uint32_t)dynamicStates.size(), .pDynamicStates = dynamicStates.data()};

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1, .pSetLayouts = &descriptorSetLayout};
    vkCreatePipelineLayout(ctx.device, &pipelineLayoutInfo, nullptr, &pipelineLayout);

    VkGraphicsPipelineCreateInfo pipelineInfo{.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = 2, .pStages = shaderStages, .pVertexInputState = &vertexInputInfo, .pInputAssemblyState = &inputAssembly, .pViewportState = &viewportState, .pRasterizationState = &rasterizer, .pMultisampleState = &multisampling, .pDepthStencilState = &depthStencil, .pColorBlendState = &colorBlending, .pDynamicState = &dynamicState, .layout = pipelineLayout, .renderPass = ctx.render_pass, .subpass = 0};

    vkCreateGraphicsPipelines(ctx.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline);

    vkDestroyShaderModule(ctx.device, fragMod, nullptr);
    vkDestroyShaderModule(ctx.device, vertMod, nullptr);

    return true;
}

auto Shutdown() -> void {
    auto& ctx = graphics::internal::context;
    vkDeviceWaitIdle(ctx.device);

    vkDestroyPipeline(ctx.device, graphicsPipeline, nullptr);
    vkDestroyPipelineLayout(ctx.device, pipelineLayout, nullptr);
    vkDestroyDescriptorPool(ctx.device, descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(ctx.device, descriptorSetLayout, nullptr);

    for(int i=0; i<2; i++) vmaDestroyBuffer(ctx.allocator, ubos[i], uboAllocs[i]);
    vmaDestroyBuffer(ctx.allocator, indexBuffer, indexBufferAlloc);
    vmaDestroyBuffer(ctx.allocator, vertexBuffer, vertexBufferAlloc);
}

auto Update(double time) -> void {
    auto& ctx = graphics::internal::context;
    float dt = static_cast<float>(time - lastTime);
    lastTime = time;

    // --- ИНТЕРФЕЙС IMGUI ---
    ImGui::Begin("Laboratory Work 1");
    ImGui::Text("Camera Settings");
    ImGui::RadioButton("Perspective", &projectionType, 0); ImGui::SameLine();
    ImGui::RadioButton("Ortographic", &projectionType, 1);
    ImGui::Separator();

    ImGui::Text("Outer view (Object 1)");
    ImGui::ColorEdit3("Color", glm::value_ptr(guiColor));
    ImGui::Separator();

    ImGui::Text("Transformation (Object 1)");
    ImGui::SliderFloat3("Position", glm::value_ptr(objPosition), -5.0f, 5.0f);
    ImGui::SliderFloat3("Rotation", glm::value_ptr(objRotation), 0.0f, 360.0f);
    ImGui::SliderFloat3("Scale", glm::value_ptr(objScale), 0.1f, 3.0f);
    ImGui::Separator();

    ImGui::Text("Difficult Animation (Lemniscate)");
    ImGui::Checkbox("Animation", &isAnimating);
    ImGui::SliderFloat("Speed", &animSpeed, 0.1f, 5.0f);
    ImGui::SliderFloat("Radius", &animRadius, 1.0f, 10.0f);
    ImGui::End();

    // --- МАТЕМАТИКА ---
    if (isAnimating) animTime += dt * animSpeed;

    float aspect = ctx.swapchain_extent.width / (float)ctx.swapchain_extent.height;

    glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 3.0f, 10.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = (projectionType == 0)
        ? glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f)
        : glm::ortho(-5.0f * aspect, 5.0f * aspect, -5.0f, 5.0f, -10.0f, 100.0f);
    proj[1][1] *= -1; // Исправление Y для Vulkan

    // Объект 1: Управляемый/Анимируемый
    UniformBufferObject uboData1{.view = view, .proj = proj, .uiColor = glm::vec4(guiColor, 1.0f)};
    glm::mat4 model1 = glm::mat4(1.0f);
    if (isAnimating) {
        // Сложная траектория (восьмерка)
        float denom = 1.0f + sin(animTime) * sin(animTime);
        model1 = glm::translate(model1, glm::vec3(animRadius * cos(animTime) / denom, 0.0f, animRadius * cos(animTime) * sin(animTime) / denom));
        model1 = glm::rotate(model1, animTime * 2.0f, glm::vec3(0, 1, 0));
    } else {
        model1 = glm::translate(model1, objPosition);
        model1 = glm::rotate(model1, glm::radians(objRotation.x), glm::vec3(1, 0, 0));
        model1 = glm::rotate(model1, glm::radians(objRotation.y), glm::vec3(0, 1, 0));
        model1 = glm::rotate(model1, glm::radians(objRotation.z), glm::vec3(0, 0, 1));
    }
    uboData1.model = glm::scale(model1, objScale);

    void* data;
    vmaMapMemory(ctx.allocator, uboAllocs[0], &data);
    memcpy(data, &uboData1, sizeof(uboData1));
    vmaUnmapMemory(ctx.allocator, uboAllocs[0]);

    // Объект 2: Статичный (Для доп. задания 6)
    UniformBufferObject uboData2{.view = view, .proj = proj, .uiColor = glm::vec4(0.5f, 0.5f, 1.0f, 1.0f)};
    glm::mat4 model2 = glm::translate(glm::mat4(1.0f), glm::vec3(-4.0f, 0.0f, -2.0f));
    model2 = glm::rotate(model2, static_cast<float>(time * 0.5), glm::vec3(1, 1, 0));
    uboData2.model = model2;

    vmaMapMemory(ctx.allocator, uboAllocs[1], &data);
    memcpy(data, &uboData2, sizeof(uboData2));
    vmaUnmapMemory(ctx.allocator, uboAllocs[1]);
}

auto Render(const graphics::internal::FrameData &frame_data) -> void {
    auto& ctx = graphics::internal::context;
    VkCommandBuffer cmd = frame_data.command_buffer;

    VkCommandBufferBeginInfo begin_info{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    vkBeginCommandBuffer(cmd, &begin_info);

    VkClearValue clear_values[2] = { {{0.15f, 0.15f, 0.15f, 1.0f}}, {1.0f, 0} };
    VkRenderPassBeginInfo rp_begin{.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, .renderPass = ctx.render_pass, .framebuffer = frame_data.framebuffer, .renderArea = {.offset = {0, 0}, .extent = ctx.swapchain_extent}, .clearValueCount = 2, .pClearValues = clear_values};

    vkCmdBeginRenderPass(cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

    VkViewport viewport{.x = 0.0f, .y = 0.0f, .width = (float)ctx.swapchain_extent.width, .height = (float)ctx.swapchain_extent.height, .minDepth = 0.0f, .maxDepth = 1.0f};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    VkRect2D scissor{.offset = {0, 0}, .extent = ctx.swapchain_extent};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, offsets);
    vkCmdBindIndexBuffer(cmd, indexBuffer, 0, VK_INDEX_TYPE_UINT16);

    // Отрисовка Объекта 1 (Set 0)
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets[0], 0, nullptr);
    vkCmdDrawIndexed(cmd, static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);

    // Отрисовка Объекта 2 (Set 1)
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets[1], 0, nullptr);
    vkCmdDrawIndexed(cmd, static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);

    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);
}

} // namespace application