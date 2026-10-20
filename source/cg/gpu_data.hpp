#pragma once

#include "scene.hpp"

#include <cstdint>

namespace cg {

    class alignas(16) SceneUniform {
    public:
        static auto From(
            const Scene &scene,
            const float aspect
        ) -> SceneUniform {
            const auto &light = scene.directionalLight;

            SceneUniform data;
            data.viewProjection      = scene.camera.ProjectionMatrix(aspect) * scene.camera.ViewMatrix();
            data.cameraPosition      = glm::vec4{scene.camera.position, 1};
            data.ambient             = glm::vec4{scene.ambientColor * scene.ambientIntensity, 0};
            data.lightDirection      = glm::vec4{normalizedOr(light.direction, {0, -1, 0}), 0};
            data.directionalRadiance = glm::vec4{light.color * (light.enabled ? light.intensity : 0.0f), 0};
            data.lightCounts         = {
                static_cast<std::uint32_t>(scene.pointLights.size()),
                static_cast<std::uint32_t>(scene.spotLights.size()),
                scene.lightingEnabled ? 1u : 0u, 0u
            };

            return data;
        }

    public:
        glm::mat4 viewProjection{1.0f};
        glm::vec4 cameraPosition{0.0f};
        glm::vec4 ambient{0.0f};
        glm::vec4 lightDirection{0.0f};
        glm::vec4 directionalRadiance{0.0f};
        glm::uvec4 lightCounts{0u}; // point count, spot count, lighting enabled, reserved
    };

    class alignas(16) ModelUniform {
    public:
        static auto From(
            const SceneObject &object
        ) -> ModelUniform {
            ModelUniform data;
            data.model        = object.ModelMatrix();
            data.normalMatrix = glm::transpose(glm::inverse(data.model));
            data.albedo       = glm::vec4{object.material.albedo, object.material.vertexColorWeight};
            data.specular     = glm::vec4{object.material.specular, object.material.shininess};
            return data;
        }

        static auto Unlit(
            const glm::mat4 &model,
            const glm::vec3 color
        ) -> ModelUniform {
            ModelUniform data;
            data.model    = model;
            data.emission = glm::vec4{color, 1};

            return data;
        }

    public:
        glm::mat4 model{1.0f};
        glm::mat4 normalMatrix{1.0f};
        glm::vec4 albedo{1.0f};                      // alpha: procedural vertex color weight
        glm::vec4 specular{0.5f, 0.5f, 0.5f, 48.0f}; // alpha: shininess
        glm::vec4 emission{0.0f};                    // alpha: unlit flag
    };

    class alignas(16) PointLightData {
    public:
        explicit PointLightData(
            const PointLight &light
        ) : position{light.position, 1},
            radiance{light.Radiance(), 0} {}

    public:
        glm::vec4 position;
        glm::vec4 radiance;
    };

    class alignas(16) SpotLightData {
    public:
        explicit SpotLightData(
            const SpotLight &light
        ) : position{light.emitter.position, 1},
            radiance{light.emitter.Radiance(), 0},
            direction{light.NormalizedDirection(), 0},
            coneCosines{
                std::cos(glm::radians(light.innerAngleDegrees)),
                std::cos(glm::radians(light.outerAngleDegrees)), 0, 0
            } {}

    public:
        glm::vec4 position;
        glm::vec4 radiance;
        glm::vec4 direction;
        glm::vec4 coneCosines; // inner, outer, reserved, reserved
    };

} // namespace cg
