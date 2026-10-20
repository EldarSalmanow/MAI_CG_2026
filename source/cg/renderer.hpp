#pragma once

#include "geometry.hpp"
#include "gpu_data.hpp"
#include "vulkan_resources.hpp"

#include <array>
#include <bit>
#include <filesystem>
#include <limits>
#include <memory>
#include <vector>

namespace cg {
    
    inline auto MakeDescriptorLayout(
        const VkDevice device,
        const std::span<const VkDescriptorSetLayoutBinding> bindings
    ) -> DescriptorSetLayout {
        DescriptorSetLayout layout{device};

        const VkDescriptorSetLayoutCreateInfo info{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = static_cast<std::uint32_t>(bindings.size()),
            .pBindings = bindings.data()
        };

        CheckVulkan(
            vkCreateDescriptorSetLayout(device, &info, nullptr, layout.Put()),
            "vkCreateDescriptorSetLayout"
        );

        return layout;
    }

    inline auto WriteBufferDescriptor(
        const VkDevice device,
        const VkDescriptorSet set,
        const std::uint32_t binding,
        const VkDescriptorType type,
        const Buffer &buffer
    ) -> void {
        const VkDescriptorBufferInfo bufferInfo{
            .buffer = buffer.Get(),
            .offset = 0,
            .range = buffer.Size()
        };

        const VkWriteDescriptorSet write{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = set,
            .dstBinding = binding,
            .descriptorCount = 1,
            .descriptorType = type,
            .pBufferInfo = &bufferInfo
        };

        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }

    class GPUMesh {
    public:
        GPUMesh(
            const VmaAllocator allocator,
            const Mesh &mesh
        ) : vertices_{allocator, mesh.vertices.size() * sizeof(Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT},
            indices_{allocator, mesh.indices.size() * sizeof(std::uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT},
            indexCount_{static_cast<std::uint32_t>(mesh.indices.size())} {
            vertices_.Upload(std::as_bytes(std::span{mesh.vertices}));
            indices_.Upload(std::as_bytes(std::span{mesh.indices}));
        }

    public:
        auto Draw(const VkCommandBuffer commands) const -> void {
            const auto vertexBuffer = vertices_.Get();

            constexpr VkDeviceSize offset = 0;

            vkCmdBindVertexBuffers(commands, 0, 1, &vertexBuffer, &offset);
            vkCmdBindIndexBuffer(commands, indices_.Get(), 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(commands, indexCount_, 1, 0, 0, 0);
        }

    private:
        Buffer vertices_;
        Buffer indices_;
        std::uint32_t indexCount_;
    };

    class DescriptorArena {
    public:
        DescriptorArena(
            const graphics::internal::Context &context,
            const VkDescriptorSetLayout layout,
            const std::uint32_t capacity
        ) : pool_{context.device},
            sets_(capacity) {
            buffers_.reserve(capacity);
            for (std::uint32_t i = 0; i < capacity; ++i) {
                buffers_.emplace_back(context.allocator,
                                      sizeof(ModelUniform),
                                      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
            }

            const VkDescriptorPoolSize size{
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                capacity
            };
            const VkDescriptorPoolCreateInfo poolInfo{
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                .maxSets = capacity,
                .poolSizeCount = 1,
                .pPoolSizes = &size
            };

            CheckVulkan(
                vkCreateDescriptorPool(context.device, &poolInfo, nullptr, pool_.Put()),
                "vkCreateDescriptorPool"
            );

            const std::vector layouts(capacity, layout);
            const VkDescriptorSetAllocateInfo allocationInfo{
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .descriptorPool = pool_.Get(),
                .descriptorSetCount = capacity,
                .pSetLayouts = layouts.data()
            };

            CheckVulkan(
                vkAllocateDescriptorSets(context.device, &allocationInfo, sets_.data()),
                "vkAllocateDescriptorSets"
            );

            for (std::uint32_t i = 0; i < capacity; ++i) {
                WriteBufferDescriptor(context.device,
                                      sets_[i],
                                      0,
                                      VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                      buffers_[i]);
            }
        }

    public:
        auto Capacity() const -> std::size_t {
            return sets_.size();
        }

        auto Set(
            const std::size_t index
        ) const -> VkDescriptorSet {
            return sets_[index];
        }

        auto Upload(
            const std::size_t index,
            const ModelUniform &data
        ) const -> void {
            buffers_[index].UploadValue(data);
        }

    private:
        std::vector<Buffer> buffers_;
        DescriptorPool pool_;
        std::vector<VkDescriptorSet> sets_;
    };

    class SceneBindings {
    public:
        SceneBindings(
            const graphics::internal::Context &context,
            const VkDescriptorSetLayout layout,
            const VkDeviceSize maxStorageBytes
        ) : device_{context.device},
            uniform_{context.allocator, sizeof(SceneUniform), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT},
            points_{context.allocator, maxStorageBytes}, spots_{context.allocator, maxStorageBytes},
            pool_{context.device} {
            constexpr std::array sizes{
                VkDescriptorPoolSize{
                    .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .descriptorCount = 1
                },
                VkDescriptorPoolSize{
                    .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    .descriptorCount = 2
                }
            };
            const VkDescriptorPoolCreateInfo info{
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                .maxSets = 1,
                .poolSizeCount = static_cast<std::uint32_t>(sizes.size()),
                .pPoolSizes = sizes.data()
            };

            CheckVulkan(
                vkCreateDescriptorPool(device_, &info, nullptr, pool_.Put()),
                "vkCreateDescriptorPool"
            );

            const VkDescriptorSetAllocateInfo allocationInfo{
                .sType =
                VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .descriptorPool = pool_.Get(),
                .descriptorSetCount = 1,
                .pSetLayouts = &layout
            };

            CheckVulkan(
                vkAllocateDescriptorSets(device_, &allocationInfo, &set_),
                "vkAllocateDescriptorSets"
            );

            WriteBufferDescriptor(device_, set_, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, uniform_);
            WriteBufferDescriptor(device_, set_, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, points_.GetBuffer());
            WriteBufferDescriptor(device_, set_, 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, spots_.GetBuffer());
        }

    public:
        auto Upload(
            const Scene &scene,
            const float aspect
        ) -> void {
            pointData_.clear();
            pointData_.reserve(scene.pointLights.size());
            for (const auto &light : scene.pointLights) {
                pointData_.emplace_back(light);
            }

            spotData_.clear();
            spotData_.reserve(scene.spotLights.size());
            for (const auto &light : scene.spotLights) {
                spotData_.emplace_back(light);
            }

            if (points_.Upload(pointData_)) {
                WriteBufferDescriptor(device_, set_, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, points_.GetBuffer());
            }

            if (spots_.Upload(spotData_)) {
                WriteBufferDescriptor(device_, set_, 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, spots_.GetBuffer());
            }

            uniform_.UploadValue(SceneUniform::From(scene, aspect));
        }

        auto Set() const -> VkDescriptorSet {
            return set_;
        }

        auto PointLimit() const -> std::size_t {
            return points_.Limit();
        }

        auto SpotLimit() const -> std::size_t {
            return spots_.Limit();
        }

    private:
        VkDevice device_;
        Buffer uniform_;
        StorageBuffer<PointLightData> points_;
        StorageBuffer<SpotLightData> spots_;
        DescriptorPool pool_;
        VkDescriptorSet set_{};
        std::vector<PointLightData> pointData_;
        std::vector<SpotLightData> spotData_;
    };

    enum class MeshKind {
        Cube,
        ConeOutline
    };

    class DrawItem {
    public:
        ModelUniform model;
        MeshKind mesh{MeshKind::Cube};
    };

    class SceneRenderer {
    public:
        explicit SceneRenderer(
            const graphics::internal::Context &context
        ) : context_{context},
            sceneLayout_{CreateSceneLayout(context.device)},
            modelLayout_{CreateModelLayout(context.device)},
            pipelineLayout_{context.device},
            solidPipeline_{context.device},
            linePipeline_{context.device},
            cube_{context.allocator, MeshFactory::Cube()},
            cone_{context.allocator, MeshFactory::ConeOutline()},
            sceneBindings_{context, sceneLayout_.Get(), StorageLimit(context.physical_device)} {
            const std::array layouts{sceneLayout_.Get(), modelLayout_.Get()};
            const VkPipelineLayoutCreateInfo info{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                .setLayoutCount = static_cast<std::uint32_t>(layouts.size()),
                .pSetLayouts = layouts.data()
            };

            CheckVulkan(
                vkCreatePipelineLayout(context.device, &info, nullptr, pipelineLayout_.Put()),
                "vkCreatePipelineLayout"
            );

            const std::filesystem::path shaderDirectory{CG_SHADER_DIR};
            const ShaderModule vertexShader{context.device, shaderDirectory / "shader.vert.spv"};
            const ShaderModule fragmentShader{context.device, shaderDirectory / "shader.frag.spv"};

            solidPipeline_ = CreatePipeline(vertexShader, fragmentShader, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
            linePipeline_ = CreatePipeline(vertexShader, fragmentShader, VK_PRIMITIVE_TOPOLOGY_LINE_LIST);
        }

    public:
        [[nodiscard]] std::size_t PointLightLimit() const {
            return sceneBindings_.PointLimit();
        }

        [[nodiscard]] std::size_t SpotLightLimit() const {
            return sceneBindings_.SpotLimit();
        }

        auto Render(
            const Scene &scene,
            const graphics::internal::FrameData &frame
        ) -> void {
            if (frame.command_buffer == VK_NULL_HANDLE || frame.framebuffer == VK_NULL_HANDLE) {
                throw std::runtime_error{"[ERROR] The graphics backend returned an invalid frame!"};
            }

            const auto extent = context_.swapchain_extent;
            if (extent.width == 0 || extent.height == 0) {
                throw std::runtime_error{"[ERROR] The graphics backend returned a zero-sized frame!"};
            }

            BuildDrawList(scene);
            if (!models_ || models_->Capacity() < draws_.size()) {
                const auto capacity = std::bit_ceil(std::max(std::size_t{4}, draws_.size()));
                if (capacity > std::numeric_limits<std::uint32_t>::max()) {
                    throw std::length_error{"[ERROR] Too many drawable models!"};
                }

                models_ = std::make_unique<DescriptorArena>(context_, modelLayout_.Get(),
                                                            static_cast<std::uint32_t>(capacity));
            }

            sceneBindings_.Upload(scene, static_cast<float>(extent.width) / static_cast<float>(extent.height));

            for (std::size_t i = 0; i < draws_.size(); ++i) {
                models_->Upload(i, draws_[i].model);
            }

            Record(frame);
        }

    private:
        static auto StorageLimit(
            const VkPhysicalDevice device
        ) -> VkDeviceSize {
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(device, &properties);
            return properties.limits.maxStorageBufferRange;
        }

        static auto CreateSceneLayout(
            const VkDevice device
        ) -> DescriptorSetLayout {
            constexpr std::array bindings{
                VkDescriptorSetLayoutBinding{
                    0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr
                },
                VkDescriptorSetLayoutBinding{
                    1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                    VK_SHADER_STAGE_FRAGMENT_BIT, nullptr
                },
                VkDescriptorSetLayoutBinding{
                    2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                    VK_SHADER_STAGE_FRAGMENT_BIT, nullptr
                }
            };

            return MakeDescriptorLayout(device, bindings);
        }

        static auto CreateModelLayout(
            const VkDevice device
        ) -> DescriptorSetLayout {
            constexpr std::array bindings{
                VkDescriptorSetLayoutBinding{
                    0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr
                }
            };

            return MakeDescriptorLayout(device, bindings);
        }

        auto CreatePipeline(
            const ShaderModule &vertex,
            const ShaderModule &fragment,
            const VkPrimitiveTopology topology
        ) const -> Pipeline {
            const std::array stages{
                VkPipelineShaderStageCreateInfo{
                    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                    .stage = VK_SHADER_STAGE_VERTEX_BIT,
                    .module = vertex.Get(),
                    .pName = "main"
                },
                VkPipelineShaderStageCreateInfo{
                    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                    .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                    .module = fragment.Get(),
                    .pName = "main"
                }
            };

            constexpr VkVertexInputBindingDescription binding{
                .binding = 0,
                .stride = sizeof(Vertex),
                .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
            };
            constexpr std::array attributes{
                VkVertexInputAttributeDescription{
                    .location = 0,
                    .binding = 0,
                    .format = VK_FORMAT_R32G32B32_SFLOAT,
                    .offset = offsetof(Vertex, position)
                },
                VkVertexInputAttributeDescription{
                    .location = 1,
                    .binding = 0,
                    .format = VK_FORMAT_R32G32B32_SFLOAT,
                    .offset = offsetof(Vertex, normal)
                },
                VkVertexInputAttributeDescription{
                    .location = 2,
                    .binding = 0,
                    .format = VK_FORMAT_R32G32B32_SFLOAT,
                    .offset = offsetof(Vertex, color)
                }
            };
            const VkPipelineVertexInputStateCreateInfo vertexInput{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
                .vertexBindingDescriptionCount = 1,
                .pVertexBindingDescriptions = &binding,
                .vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size()),
                .pVertexAttributeDescriptions = attributes.data()
            };
            const VkPipelineInputAssemblyStateCreateInfo assembly{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                .topology = topology
            };
            const VkPipelineViewportStateCreateInfo viewport{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                .viewportCount = 1,
                .scissorCount = 1
            };
            const VkPipelineRasterizationStateCreateInfo rasterizer{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                .polygonMode = VK_POLYGON_MODE_FILL,
                .cullMode = static_cast<VkCullModeFlags>(
                    topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE),
                .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
                .lineWidth = 1.0f
            };
            const VkPipelineMultisampleStateCreateInfo multisampling{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
            };
            const VkPipelineDepthStencilStateCreateInfo depth{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
                .depthTestEnable = VK_TRUE,
                .depthWriteEnable = topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST ? VK_TRUE : VK_FALSE,
                .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL
            };
            constexpr VkPipelineColorBlendAttachmentState attachment{
                .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                                  VK_COLOR_COMPONENT_A_BIT
            };
            const VkPipelineColorBlendStateCreateInfo blend{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                .attachmentCount = 1,
                .pAttachments = &attachment
            };
            constexpr std::array dynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
            const VkPipelineDynamicStateCreateInfo dynamic{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                .dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size()),
                .pDynamicStates = dynamicStates.data()
            };
            const VkGraphicsPipelineCreateInfo info{
                .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                .stageCount = static_cast<std::uint32_t>(stages.size()),
                .pStages = stages.data(),
                .pVertexInputState = &vertexInput,
                .pInputAssemblyState = &assembly,
                .pViewportState = &viewport,
                .pRasterizationState = &rasterizer,
                .pMultisampleState = &multisampling,
                .pDepthStencilState = &depth,
                .pColorBlendState = &blend,
                .pDynamicState = &dynamic,
                .layout = pipelineLayout_.Get(),
                .renderPass = context_.render_pass,
                .subpass = 0
            };

            Pipeline pipeline{context_.device};

            CheckVulkan(
                vkCreateGraphicsPipelines(context_.device, VK_NULL_HANDLE, 1, &info, nullptr, pipeline.Put()),
                "vkCreateGraphicsPipelines"
            );

            return pipeline;
        }

        auto BuildDrawList(
            const Scene &scene
        ) -> void {
            draws_.clear();

            for (const auto &object : scene.objects) {
                draws_.push_back(DrawItem{ModelUniform::From(object), MeshKind::Cube});
            }

            const auto marker = [this](const PointLight &light) {
                auto model = glm::translate(glm::mat4{1.0f}, light.position);

                model = glm::scale(model, glm::vec3{0.12f});

                draws_.push_back(DrawItem{
                    .model = ModelUniform::Unlit(model, light.color),
                    .mesh = MeshKind::Cube
                });
            };

            if (scene.showLightMarkers) {
                for (const auto &light : scene.pointLights) {
                    marker(light);
                }
                for (const auto &light : scene.spotLights) {
                    marker(light.emitter);
                }
            }

            if (scene.showSpotCones) {
                for (const auto &light : scene.spotLights) {
                    draws_.push_back(DrawItem{
                        .model = ModelUniform::Unlit(light.ConeMatrix(scene.coneLength), {1, 1, 1}),
                        .mesh = MeshKind::ConeOutline
                    });
                }
            }
        }

        auto Record(
            const graphics::internal::FrameData &frame
        ) const -> void {
            const auto commands = frame.command_buffer;

            CheckVulkan(
                vkResetCommandBuffer(commands, 0),
                "vkResetCommandBuffer"
            );

            constexpr VkCommandBufferBeginInfo begin{
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
            };

            CheckVulkan(
                vkBeginCommandBuffer(commands, &begin),
                "CheckVulkan"
            );

            constexpr std::array clears{
                VkClearValue{
                    .color = {
                        .float32 = {0.035f, 0.045f, 0.065f, 1.0f}
                    }
                },
                VkClearValue{
                    .depthStencil = {
                        .depth = 1.0f,
                        .stencil = 0
                    }
                }
            };

            const VkRenderPassBeginInfo pass{
                .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                .renderPass = context_.render_pass,
                .framebuffer = frame.framebuffer,
                .renderArea = {
                    .offset = {
                        .x = 0,
                        .y = 0
                    },
                    .extent = context_.swapchain_extent
                },
                .clearValueCount = static_cast<std::uint32_t>(clears.size()),
                .pClearValues = clears.data()
            };

            vkCmdBeginRenderPass(commands, &pass, VK_SUBPASS_CONTENTS_INLINE);

            const VkViewport viewport{
                .x = 0,
                .y = 0,
                .width = static_cast<float>(context_.swapchain_extent.width),
                .height = static_cast<float>(context_.swapchain_extent.height),
                .minDepth = 0,
                .maxDepth = 1
            };
            const VkRect2D scissor{
                .offset = {
                    .x = 0,
                    .y = 0
                },
                .extent = context_.swapchain_extent
            };
            vkCmdSetViewport(commands, 0, 1, &viewport);
            vkCmdSetScissor(commands, 0, 1, &scissor);

            const auto sceneSet = sceneBindings_.Set();
            vkCmdBindDescriptorSets(commands,
                                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipelineLayout_.Get(), 0, 1,
                                    &sceneSet, 0, nullptr);

            VkPipeline boundPipeline = VK_NULL_HANDLE;
            for (std::size_t i = 0; i < draws_.size(); ++i) {
                const auto line = draws_[i].mesh == MeshKind::ConeOutline;
                const auto pipeline = line ? linePipeline_.Get() : solidPipeline_.Get();

                if (pipeline != boundPipeline) {
                    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                    boundPipeline = pipeline;
                }

                const auto modelSet = models_->Set(i);

                vkCmdBindDescriptorSets(commands,
                                        VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        pipelineLayout_.Get(), 1, 1,
                                        &modelSet, 0, nullptr);
                (line ? cone_ : cube_).Draw(commands);
            }

            vkCmdEndRenderPass(commands);

            CheckVulkan(
                vkEndCommandBuffer(commands),
                "vkEndCommandBuffer"
            );
        }

    private:
        const graphics::internal::Context &context_;
        DescriptorSetLayout sceneLayout_;
        DescriptorSetLayout modelLayout_;
        PipelineLayout pipelineLayout_;
        Pipeline solidPipeline_;
        Pipeline linePipeline_;
        GPUMesh cube_;
        GPUMesh cone_;
        SceneBindings sceneBindings_;
        std::unique_ptr<DescriptorArena> models_;
        std::vector<DrawItem> draws_;
    };

} // namespace cg
