#pragma once

#include "scene.hpp"

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace cg {

    class alignas(16) SamplingPushConstants final {
    public:
        static auto From(
            const SamplingSettings &settings,
            const float time
        ) -> SamplingPushConstants {
            SamplingPushConstants data;
            data.tiling = settings.tiling;
            data.time = time;
            data.speed = settings.speed;
            data.blendWeight = settings.blendWeight;
            data.animationEnabled = settings.animated ? 1u : 0u;
            data.emissionStrength = settings.emissionStrength;

            return data;
        }

        glm::vec2 tiling{1.0f};
        float time{0.0f};
        float speed{0.35f};
        float blendWeight{0.5f};
        std::uint32_t animationEnabled{0};
        float emissionStrength{1.0f};
        std::uint32_t reserved{0};
    };

    class alignas(16) SceneUniform final {
    public:
        static auto From(
            const Scene &scene,
            const float aspect
        ) -> SceneUniform {
            const auto &light = scene.directionalLight;

            SceneUniform data;
            data.viewProjection = scene.camera.ProjectionMatrix(aspect) * scene.camera.ViewMatrix();
            data.cameraPosition = glm::vec4{scene.camera.position, 1};
            data.ambient = glm::vec4{scene.ambientColor * scene.ambientIntensity, 0};
            data.lightDirection = glm::vec4{NormalizedOr(light.direction, {0, -1, 0}), 0};
            data.directionalRadiance = glm::vec4{light.color * (light.enabled ? light.intensity : 0.0f), 0};
            data.pointLightCount = static_cast<std::uint32_t>(scene.pointLights.size());
            data.spotLightCount = static_cast<std::uint32_t>(scene.spotLights.size());
            data.lightingEnabled = scene.lightingEnabled ? 1u : 0u;

            return data;
        }

        glm::mat4 viewProjection{1.0f};
        glm::vec4 cameraPosition{0.0f};
        glm::vec4 ambient{0.0f};
        glm::vec4 lightDirection{0.0f};
        glm::vec4 directionalRadiance{0.0f};
        std::uint32_t pointLightCount{0};
        std::uint32_t spotLightCount{0};
        std::uint32_t lightingEnabled{0};
        std::uint32_t reserved{0};
    };

    class alignas(16) ModelUniform final {
    public:
        static auto From(
            const SceneObject &object
        ) -> ModelUniform {
            ModelUniform data;
            data.model = object.ModelMatrix();
            data.normalMatrix = glm::transpose(glm::inverse(data.model));
            data.albedo = object.material.albedo;
            data.vertexColorWeight = object.material.vertexColorWeight;
            data.specular = object.material.specular;
            data.shininess = object.material.shininess;

            return data;
        }

        static auto Unlit(
            const glm::mat4 &model,
            const glm::vec3 color
        ) -> ModelUniform {
            ModelUniform data;
            data.model = model;
            data.unlitColor = color;
            data.unlit = 1;

            return data;
        }

        glm::mat4 model{1.0f};
        glm::mat4 normalMatrix{1.0f};
        glm::vec3 albedo{1.0f};
        float vertexColorWeight{0.0f};
        glm::vec3 specular{0.5f};
        float shininess{48.0f};
        glm::vec3 unlitColor{0.0f};
        std::uint32_t unlit{0};
    };

    class alignas(16) PointLightData final {
    public:
        explicit PointLightData(
            const PointLight &light
        )
            : position{light.position, 1},
              radiance{light.Radiance(), 0} {}

        glm::vec4 position;
        glm::vec4 radiance;
    };

    class alignas(16) SpotLightData final {
    public:
        explicit SpotLightData(
            const SpotLight &light
        )
            : position{light.emitter.position, 1},
              radiance{light.emitter.Radiance(), 0},
              direction{light.NormalizedDirection(), 0},
              innerConeCosine{std::cos(glm::radians(light.innerAngleDegrees))},
              outerConeCosine{std::cos(glm::radians(light.outerAngleDegrees))} {}

        glm::vec4 position;
        glm::vec4 radiance;
        glm::vec4 direction;
        float innerConeCosine;
        float outerConeCosine;
        glm::vec2 reserved{0.0f};
    };

    // Keep these offsets identical to the uniform and storage blocks in GLSL.
    static_assert(sizeof(SamplingPushConstants) == 32);
    static_assert(offsetof(SamplingPushConstants, blendWeight) == 16);
    static_assert(offsetof(SamplingPushConstants, animationEnabled) == 20);
    static_assert(offsetof(SamplingPushConstants, emissionStrength) == 24);

    static_assert(sizeof(SceneUniform) == 144);
    static_assert(offsetof(SceneUniform, pointLightCount) == 128);
    static_assert(offsetof(SceneUniform, lightingEnabled) == 136);

    static_assert(sizeof(ModelUniform) == 176);
    static_assert(offsetof(ModelUniform, vertexColorWeight) == 140);
    static_assert(offsetof(ModelUniform, shininess) == 156);
    static_assert(offsetof(ModelUniform, unlitColor) == 160);
    static_assert(offsetof(ModelUniform, unlit) == 172);
    static_assert(std::is_trivially_copyable_v<ModelUniform>);

    static_assert(sizeof(PointLightData) == 32);
    static_assert(sizeof(SpotLightData) == 64);
    static_assert(offsetof(SpotLightData, innerConeCosine) == 48);
    static_assert(offsetof(SpotLightData, outerConeCosine) == 52);

} // namespace cg
