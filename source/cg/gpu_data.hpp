#pragma once

#include "scene.hpp"

#include <cstdint>

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

    inline constexpr std::uint32_t ShadowPushOffset = sizeof(SamplingPushConstants);

    class alignas(16) ShadowPushConstants final {
    public:
        glm::mat4 lightViewProjection{1.0f};
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
            data.ambientRadiance = glm::vec4{scene.ambientColor * scene.ambientIntensity, 0};
            data.directionalLightDirection = glm::vec4{NormalizedOr(light.direction, {0, -1, 0}), 0};
            data.directionalLightRadiance = glm::vec4{light.color * (light.enabled ? light.intensity : 0.0f), 0};
            data.pointLightCount = static_cast<std::uint32_t>(scene.pointLights.size());
            data.spotLightCount = static_cast<std::uint32_t>(scene.spotLights.size());
            data.lightingEnabled = scene.lightingEnabled ? 1u : 0u;
            data.directionalLightViewProjection = scene.shadows.DirectionalMatrix(light);
            data.directionalShadowEnabled = scene.shadows.enabled && scene.shadows.directionalEnabled && light.enabled;

            if (scene.shadows.spotLightIndex < scene.spotLights.size()) {
                const auto &spot = scene.spotLights[scene.shadows.spotLightIndex];
                data.spotLightViewProjection = scene.shadows.SpotMatrix(spot);
                data.shadowSpotLightIndex = static_cast<std::uint32_t>(scene.shadows.spotLightIndex);
                data.spotShadowEnabled = scene.shadows.enabled && scene.shadows.spotEnabled && spot.emitter.enabled;
            }

            return data;
        }

        glm::mat4 viewProjection{1.0f};
        glm::vec4 cameraPosition{0.0f};
        glm::vec4 ambientRadiance{0.0f};
        glm::vec4 directionalLightDirection{0.0f};
        glm::vec4 directionalLightRadiance{0.0f};
        std::uint32_t pointLightCount{0};
        std::uint32_t spotLightCount{0};
        std::uint32_t lightingEnabled{0};
        std::uint32_t reserved{0};
        glm::mat4 directionalLightViewProjection{1.0f};
        glm::mat4 spotLightViewProjection{1.0f};
        std::uint32_t directionalShadowEnabled{0};
        std::uint32_t spotShadowEnabled{0};
        std::uint32_t shadowSpotLightIndex{0};
        std::uint32_t shadowReserved{0};
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

} // namespace cg
