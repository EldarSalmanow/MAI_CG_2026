#include "geometry.hpp"
#include "gpu_data.hpp"
#include "renderer.hpp"
#include "texture.hpp"

#include <array>
#include <bit>
#include <limits>
#include <map>
#include <vector>

namespace cg {
    namespace {

        enum class MeshKind {
            Cube,
            ConeOutline
        };

        class DrawItem final {
        public:
            ModelUniform model;
            MeshKind mesh{MeshKind::Cube};
            MaterialPreset material{MaterialPreset::Solid};
            SamplingPushConstants sampling;
        };

        class GpuMesh final {
        public:
            GpuMesh(
                const UploadContext &uploads,
                const Mesh &mesh
            )
                : vertices_{uploads.UploadBuffer(
                      std::as_bytes(std::span{mesh.vertices}),
                      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
                  )},
                  indices_{
                      uploads.UploadBuffer(std::as_bytes(std::span{mesh.indices}), VK_BUFFER_USAGE_INDEX_BUFFER_BIT)
                  },
                  indexCount_{static_cast<std::uint32_t>(mesh.indices.size())} {}

            auto Draw(
                const VkCommandBuffer commands
            ) const -> void {
                const auto vertices = vertices_.Get();
                constexpr VkDeviceSize offset = 0;
                vkCmdBindVertexBuffers(commands, 0, 1, &vertices, &offset);
                vkCmdBindIndexBuffer(commands, indices_.Get(), 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(commands, indexCount_, 1, 0, 0, 0);
            }

        private:
            Buffer vertices_;
            Buffer indices_;
            std::uint32_t indexCount_;
        };

        auto DeviceLimits(
            const VkPhysicalDevice device
        ) -> VkPhysicalDeviceLimits {
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(device, &properties);
            return properties.limits;
        }

        auto ModelStride(
            const VkPhysicalDeviceLimits &limits
        ) -> VkDeviceSize {
            if (sizeof(ModelUniform) > limits.maxUniformBufferRange) {
                throw std::runtime_error{"ModelUniform exceeds maxUniformBufferRange"};
            }
            const auto alignment = std::max(VkDeviceSize{1}, limits.minUniformBufferOffsetAlignment);
            return (sizeof(ModelUniform) + alignment - 1) / alignment * alignment;
        }

        auto MakeDescriptorLayout(
            const VkDevice device,
            const std::span<const VkDescriptorSetLayoutBinding> bindings
        ) -> DescriptorSetLayout {
            DescriptorSetLayout layout{device};
            const VkDescriptorSetLayoutCreateInfo info{
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                .bindingCount = static_cast<std::uint32_t>(bindings.size()),
                .pBindings = bindings.data(),
            };
            CheckVulkan(
                vkCreateDescriptorSetLayout(device, &info, nullptr, layout.Put()), "vkCreateDescriptorSetLayout"
            );
            return layout;
        }

        auto WriteBufferDescriptor(
            const VkDevice device,
            const VkDescriptorSet set,
            const std::uint32_t binding,
            const VkDescriptorType type,
            const Buffer &buffer,
            const VkDeviceSize range = VK_WHOLE_SIZE
        ) -> void {
            const VkDescriptorBufferInfo bufferInfo{buffer.Get(), 0, range == VK_WHOLE_SIZE ? buffer.Size() : range};
            const VkWriteDescriptorSet write{
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = set,
                .dstBinding = binding,
                .descriptorCount = 1,
                .descriptorType = type,
                .pBufferInfo = &bufferInfo,
            };
            vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        }

        class RenderResources final {
        public:
            RenderResources(
                const UploadContext &uploads,
                const std::filesystem::path &directory
            )
                : context_{uploads.Context()},
                  limits_{DeviceLimits(context_.physical_device)},
                  sceneLayout_{CreateSceneLayout(context_.device)},
                  materialLayout_{CreateMaterialLayout(context_.device)},
                  sceneUniform_{context_.allocator, sizeof(SceneUniform), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT},
                  points_{context_.allocator, limits_.maxStorageBufferRange},
                  spots_{context_.allocator, limits_.maxStorageBufferRange},
                  modelStride_{ModelStride(limits_)},
                  models_{context_.allocator, 4 * modelStride_, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT},
                  modelBytes_(static_cast<std::size_t>(models_.Size())),
                  sampler_{context_.device},
                  pool_{context_.device} {
                CreateSampler();
                AllocateDescriptors();
                WriteBufferDescriptor(context_.device, sceneSet_, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, sceneUniform_);
                WriteBufferDescriptor(
                    context_.device, sceneSet_, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, points_.GetBuffer()
                );
                WriteBufferDescriptor(
                    context_.device, sceneSet_, 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, spots_.GetBuffer()
                );
                WriteModelDescriptors();
                LoadMaterials(uploads, directory);
            }

            auto Layouts() const -> std::array<VkDescriptorSetLayout, 2> {
                return {sceneLayout_.Get(), materialLayout_.Get()};
            }

            auto PointLightLimit() const noexcept -> std::size_t {
                return points_.Limit();
            }

            auto SpotLightLimit() const noexcept -> std::size_t {
                return spots_.Limit();
            }

            auto Update(
                const Scene &scene,
                const float aspect,
                const std::span<const DrawItem> draws
            ) -> void {
                sceneUniform_.UploadValue(SceneUniform::From(scene, aspect));
                UploadLights(points_, pointData_, scene.pointLights, 1);
                UploadLights(spots_, spotData_, scene.spotLights, 2);
                UploadModels(draws);
            }

            auto Bind(
                const VkCommandBuffer commands,
                const VkPipelineLayout layout,
                const MaterialPreset material,
                const std::size_t index
            ) const -> void {
                if (index >= models_.Size() / modelStride_) {
                    throw std::out_of_range{"Invalid model dynamic offset"};
                }
                const std::array sets{sceneSet_, materialSets_.at(MaterialIndex(material))};
                const auto offset = static_cast<std::uint32_t>(index * modelStride_);
                vkCmdBindDescriptorSets(
                    commands, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, static_cast<std::uint32_t>(sets.size()),
                    sets.data(), 1, &offset
                );
            }

        private:
            static auto CreateSceneLayout(
                const VkDevice device
            ) -> DescriptorSetLayout {
                constexpr std::array bindings{
                    VkDescriptorSetLayoutBinding{
                        0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr
                    },
                    VkDescriptorSetLayoutBinding{
                        1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr
                    },
                    VkDescriptorSetLayoutBinding{
                        2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr
                    }
                };
                return MakeDescriptorLayout(device, bindings);
            }

            static auto CreateMaterialLayout(
                const VkDevice device
            ) -> DescriptorSetLayout {
                constexpr std::array bindings{
                    VkDescriptorSetLayoutBinding{
                        0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1,
                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr
                    },
                    VkDescriptorSetLayoutBinding{
                        1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr
                    },
                    VkDescriptorSetLayoutBinding{
                        2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr
                    },
                    VkDescriptorSetLayoutBinding{
                        3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr
                    }
                };
                return MakeDescriptorLayout(device, bindings);
            }

            auto CreateSampler() -> void {
                const VkSamplerCreateInfo info{
                    .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                    .magFilter = VK_FILTER_LINEAR,
                    .minFilter = VK_FILTER_LINEAR,
                    .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
                    .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                    .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                    .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                    .maxLod = VK_LOD_CLAMP_NONE,
                };
                CheckVulkan(vkCreateSampler(context_.device, &info, nullptr, sampler_.Put()), "vkCreateSampler");
            }

            auto AllocateDescriptors() -> void {
                constexpr auto count = static_cast<std::uint32_t>(MaterialDefinitions.size());
                constexpr std::array sizes{
                    VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
                    VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2},
                    VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, count},
                    VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, count * 3}
                };
                const VkDescriptorPoolCreateInfo poolInfo{
                    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                    .maxSets = count + 1,
                    .poolSizeCount = static_cast<std::uint32_t>(sizes.size()),
                    .pPoolSizes = sizes.data(),
                };
                CheckVulkan(
                    vkCreateDescriptorPool(context_.device, &poolInfo, nullptr, pool_.Put()), "vkCreateDescriptorPool"
                );

                std::array<VkDescriptorSetLayout, count + 1> layouts;
                layouts.fill(materialLayout_.Get());
                layouts.front() = sceneLayout_.Get();
                std::array<VkDescriptorSet, count + 1> sets{};
                const VkDescriptorSetAllocateInfo allocation{
                    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                    .descriptorPool = pool_.Get(),
                    .descriptorSetCount = static_cast<std::uint32_t>(layouts.size()),
                    .pSetLayouts = layouts.data(),
                };
                CheckVulkan(
                    vkAllocateDescriptorSets(context_.device, &allocation, sets.data()), "vkAllocateDescriptorSets"
                );
                sceneSet_ = sets.front();
                std::copy(sets.begin() + 1, sets.end(), materialSets_.begin());
            }

            auto LoadTexture(
                const UploadContext &uploads,
                const std::filesystem::path &path,
                const VkFormat format
            ) -> VkImageView {
                const auto key = std::pair{path, format};
                auto found = textures_.find(key);
                if (found == textures_.end()) {
                    const ImageData image{path, limits_.maxImageDimension2D};
                    found = textures_.try_emplace(key, uploads, image, format).first;
                }
                return found->second.View();
            }

            auto LoadMaterials(
                const UploadContext &uploads,
                const std::filesystem::path &directory
            ) -> void {
                for (std::size_t i = 0; i < MaterialDefinitions.size(); ++i) {
                    const auto &material = MaterialDefinitions[i];
                    const std::array paths{material.albedo, material.specular, material.emissive};
                    constexpr std::array formats{
                        VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_SRGB
                    };
                    for (std::uint32_t map = 0; map < paths.size(); ++map) {
                        const auto path = directory / (paths[map].empty() ? "black.png" : paths[map]);
                        const VkDescriptorImageInfo image{
                            sampler_.Get(), LoadTexture(uploads, path, formats[map]),
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                        };
                        const VkWriteDescriptorSet write{
                            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                            .dstSet = materialSets_[i],
                            .dstBinding = map + 1,
                            .descriptorCount = 1,
                            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .pImageInfo = &image,
                        };
                        vkUpdateDescriptorSets(context_.device, 1, &write, 0, nullptr);
                    }
                }
            }

            auto WriteModelDescriptors() const -> void {
                for (const auto set : materialSets_) {
                    WriteBufferDescriptor(
                        context_.device, set, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, models_,
                        sizeof(ModelUniform)
                    );
                }
            }

            template <class PackedLight, class Light>
            auto UploadLights(
                StorageBuffer<PackedLight> &buffer,
                std::vector<PackedLight> &packed,
                const std::vector<Light> &lights,
                const std::uint32_t binding
            ) -> void {
                packed.clear();
                packed.reserve(lights.size());
                for (const auto &light : lights) {
                    packed.emplace_back(light);
                }
                if (buffer.Upload(packed)) {
                    WriteBufferDescriptor(
                        context_.device, sceneSet_, binding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, buffer.GetBuffer()
                    );
                }
            }

            auto UploadModels(
                const std::span<const DrawItem> draws
            ) -> void {
                const auto required = std::max(std::size_t{1}, draws.size());
                if (required > models_.Size() / modelStride_) {
                    const auto limit = std::numeric_limits<std::uint32_t>::max() / modelStride_;
                    if (required > limit) {
                        throw std::length_error{"Model count exceeds Vulkan dynamic offset range"};
                    }
                    const auto capacity = std::min(VkDeviceSize{std::bit_ceil(required)}, limit);
                    Buffer replacement{context_.allocator, capacity * modelStride_, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT};
                    modelBytes_.resize(static_cast<std::size_t>(replacement.Size()));
                    models_ = std::move(replacement);
                    WriteModelDescriptors();
                }
                for (std::size_t i = 0; i < draws.size(); ++i) {
                    std::memcpy(modelBytes_.data() + i * modelStride_, &draws[i].model, sizeof(ModelUniform));
                }
                models_.Upload(std::span{modelBytes_}.first(static_cast<std::size_t>(draws.size() * modelStride_)));
            }

            const graphics::internal::Context &context_;
            VkPhysicalDeviceLimits limits_;
            DescriptorSetLayout sceneLayout_;
            DescriptorSetLayout materialLayout_;
            Buffer sceneUniform_;
            StorageBuffer<PointLightData> points_;
            StorageBuffer<SpotLightData> spots_;
            VkDeviceSize modelStride_;
            Buffer models_;
            std::vector<std::byte> modelBytes_;
            std::vector<PointLightData> pointData_;
            std::vector<SpotLightData> spotData_;
            Sampler sampler_;
            std::map<std::pair<std::filesystem::path, VkFormat>, Texture> textures_;
            DescriptorPool pool_;
            VkDescriptorSet sceneSet_{};
            std::array<VkDescriptorSet, MaterialDefinitions.size()> materialSets_{};
        };

        auto CreatePipeline(
            const graphics::internal::Context &context,
            const VkPipelineLayout layout,
            const ShaderModule &vertex,
            const ShaderModule &fragment,
            const VkPrimitiveTopology topology
        ) -> Pipeline {
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
                },
            };

            constexpr VkVertexInputBindingDescription binding{
                .binding = 0,
                .stride = sizeof(Vertex),
                .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
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
                    .location = 2, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, color)
                },
                VkVertexInputAttributeDescription{
                    .location = 3, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(Vertex, uv)
                },
            };
            const VkPipelineVertexInputStateCreateInfo vertexInput{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
                .vertexBindingDescriptionCount = 1,
                .pVertexBindingDescriptions = &binding,
                .vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size()),
                .pVertexAttributeDescriptions = attributes.data(),
            };
            const VkPipelineInputAssemblyStateCreateInfo assembly{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                .topology = topology,
            };
            const VkPipelineViewportStateCreateInfo viewport{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                .viewportCount = 1,
                .scissorCount = 1,
            };
            const VkPipelineRasterizationStateCreateInfo rasterizer{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                .polygonMode = VK_POLYGON_MODE_FILL,
                .cullMode = static_cast<VkCullModeFlags>(
                    topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE
                ),
                .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
                .lineWidth = 1.0f,
            };
            const VkPipelineMultisampleStateCreateInfo multisampling{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
            };
            const VkPipelineDepthStencilStateCreateInfo depth{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
                .depthTestEnable = VK_TRUE,
                .depthWriteEnable = topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST ? VK_TRUE : VK_FALSE,
                .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
            };
            constexpr VkPipelineColorBlendAttachmentState attachment{
                .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                                  VK_COLOR_COMPONENT_A_BIT
            };
            const VkPipelineColorBlendStateCreateInfo blend{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                .attachmentCount = 1,
                .pAttachments = &attachment,
            };
            constexpr std::array dynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
            const VkPipelineDynamicStateCreateInfo dynamic{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                .dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size()),
                .pDynamicStates = dynamicStates.data(),
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
                .layout = layout,
                .renderPass = context.render_pass,
                .subpass = 0,
            };

            Pipeline pipeline{context.device};

            CheckVulkan(
                vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1, &info, nullptr, pipeline.Put()),
                "vkCreateGraphicsPipelines"
            );

            return pipeline;
        }

    } // namespace

    class SceneRenderer::Implementation {
    public:
        explicit Implementation(
            const graphics::internal::Context &context
        )
            : context_{context},
              uploads_{context},
              resources_{uploads_, std::filesystem::path{CG_ASSET_DIR}},
              cube_{uploads_, MeshFactory::Cube()},
              cone_{uploads_, MeshFactory::ConeOutline()},
              pipelineLayout_{context.device},
              solidPipeline_{context.device},
              linePipeline_{context.device} {
            const auto layouts = resources_.Layouts();
            constexpr VkPushConstantRange samplingRange{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SamplingPushConstants)};
            const VkPipelineLayoutCreateInfo info{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                .setLayoutCount = static_cast<std::uint32_t>(layouts.size()),
                .pSetLayouts = layouts.data(),
                .pushConstantRangeCount = 1,
                .pPushConstantRanges = &samplingRange,
            };
            CheckVulkan(
                vkCreatePipelineLayout(context.device, &info, nullptr, pipelineLayout_.Put()), "vkCreatePipelineLayout"
            );

            const std::filesystem::path directory{CG_SHADER_DIR};
            const ShaderModule vertex{context.device, directory / "shader.vert.spv"};
            const ShaderModule fragment{context.device, directory / "shader.frag.spv"};
            solidPipeline_ =
                CreatePipeline(context, pipelineLayout_.Get(), vertex, fragment, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
            linePipeline_ =
                CreatePipeline(context, pipelineLayout_.Get(), vertex, fragment, VK_PRIMITIVE_TOPOLOGY_LINE_LIST);
        }

    public:
        auto Render(
            const Scene &scene,
            const graphics::internal::FrameData &frame
        ) -> void {
            if (frame.command_buffer == VK_NULL_HANDLE || frame.framebuffer == VK_NULL_HANDLE) {
                throw std::runtime_error{"The graphics backend returned an invalid frame"};
            }
            const auto extent = context_.swapchain_extent;
            if (extent.width == 0 || extent.height == 0) {
                throw std::runtime_error{"The graphics backend returned a zero-sized frame"};
            }
            BuildDrawList(scene);
            resources_.Update(scene, static_cast<float>(extent.width) / static_cast<float>(extent.height), draws_);
            Record(frame);
        }

        auto PointLightLimit() const noexcept -> std::size_t {
            return resources_.PointLightLimit();
        }

        auto SpotLightLimit() const noexcept -> std::size_t {
            return resources_.SpotLightLimit();
        }

    private:
        auto BuildDrawList(
            const Scene &scene
        ) -> void {
            draws_.clear();

            for (const auto &object : scene.objects) {
                draws_.push_back(DrawItem{
                    .model = ModelUniform::From(object),
                    .mesh = MeshKind::Cube,
                    .material = object.material.preset,
                    .sampling = SamplingPushConstants::From(object.material.sampling, scene.TextureTime())
                });
            }

            const auto marker = [this](const PointLight &light) {
                auto model = glm::translate(glm::mat4{1.0f}, light.position);

                model = glm::scale(model, glm::vec3{0.12f});

                draws_.push_back(DrawItem{.model = ModelUniform::Unlit(model, light.color), .mesh = MeshKind::Cube});
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
            CheckVulkan(vkResetCommandBuffer(commands, 0), "vkResetCommandBuffer");
            constexpr VkCommandBufferBeginInfo begin{
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            };
            CheckVulkan(vkBeginCommandBuffer(commands, &begin), "vkBeginCommandBuffer");

            constexpr std::array clears{
                VkClearValue{.color = {.float32 = {0.035f, 0.045f, 0.065f, 1.0f}}},
                VkClearValue{.depthStencil = {.depth = 1.0f}}
            };
            const VkRenderPassBeginInfo pass{
                .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                .renderPass = context_.render_pass,
                .framebuffer = frame.framebuffer,
                .renderArea = {.extent = context_.swapchain_extent},
                .clearValueCount = static_cast<std::uint32_t>(clears.size()),
                .pClearValues = clears.data(),
            };
            vkCmdBeginRenderPass(commands, &pass, VK_SUBPASS_CONTENTS_INLINE);
            const VkViewport viewport{
                .width = static_cast<float>(context_.swapchain_extent.width),
                .height = static_cast<float>(context_.swapchain_extent.height),
                .maxDepth = 1.0f,
            };
            const VkRect2D scissor{.extent = context_.swapchain_extent};
            vkCmdSetViewport(commands, 0, 1, &viewport);
            vkCmdSetScissor(commands, 0, 1, &scissor);

            VkPipeline boundPipeline = VK_NULL_HANDLE;
            for (std::size_t i = 0; i < draws_.size(); ++i) {
                const auto &draw = draws_[i];
                const bool line = draw.mesh == MeshKind::ConeOutline;
                const auto pipeline = line ? linePipeline_.Get() : solidPipeline_.Get();
                if (pipeline != boundPipeline) {
                    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                    boundPipeline = pipeline;
                }
                resources_.Bind(commands, pipelineLayout_.Get(), draw.material, i);
                vkCmdPushConstants(
                    commands, pipelineLayout_.Get(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SamplingPushConstants),
                    &draw.sampling
                );
                (line ? cone_ : cube_).Draw(commands);
            }
            vkCmdEndRenderPass(commands);
            CheckVulkan(vkEndCommandBuffer(commands), "vkEndCommandBuffer");
        }

    private:
        const graphics::internal::Context &context_;
        UploadContext uploads_;
        RenderResources resources_;
        GpuMesh cube_;
        GpuMesh cone_;
        PipelineLayout pipelineLayout_;
        Pipeline solidPipeline_;
        Pipeline linePipeline_;
        std::vector<DrawItem> draws_;
    };

    SceneRenderer::SceneRenderer(
        const graphics::internal::Context &context
    )
        : implementation_{std::make_unique<Implementation>(context)} {}

    SceneRenderer::~SceneRenderer() = default;

    auto SceneRenderer::Render(
        const Scene &scene,
        const graphics::internal::FrameData &frame
    ) -> void {
        implementation_->Render(scene, frame);
    }

    auto SceneRenderer::PointLightLimit() const noexcept -> std::size_t {
        return implementation_->PointLightLimit();
    }

    auto SceneRenderer::SpotLightLimit() const noexcept -> std::size_t {
        return implementation_->SpotLightLimit();
    }

} // namespace cg
