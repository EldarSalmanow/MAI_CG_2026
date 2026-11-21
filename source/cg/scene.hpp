#pragma once

#include <glm/glm.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "geometry.hpp"
#include "material.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

namespace cg {

    inline auto NormalizedOr(
        const glm::vec3 value,
        const glm::vec3 fallback
    ) -> glm::vec3 {
        const float lengthSquared = glm::dot(value, value);

        return lengthSquared > 1e-8f ? value / std::sqrt(lengthSquared) : fallback;
    }

    class Transform final {
    public:
        auto Matrix() const -> glm::mat4 {
            auto result = glm::translate(glm::mat4{1.0f}, position);

            const auto angles = glm::radians(rotationDegrees);
            result = glm::rotate(result, angles.x, {1, 0, 0});
            result = glm::rotate(result, angles.y, {0, 1, 0});
            result = glm::rotate(result, angles.z, {0, 0, 1});

            result = glm::scale(result, scale);

            return result;
        }

        glm::vec3 position{0.0f};
        glm::vec3 rotationDegrees{0.0f};
        glm::vec3 scale{1.0f};
    };

    class LemniscateAnimation final {
    public:
        auto Advance(
            const double deltaSeconds
        ) -> void {
            if (enabled && playing) {
                phase_ = std::fmod(phase_ + std::max(deltaSeconds, 0.0) * speed, 2.0 * std::numbers::pi);
            }
        }

        auto Restart() -> void {
            phase_ = 0.0;
        }

        auto Phase() const -> double {
            return phase_;
        }

        auto ModelMatrix(
            const Transform &transform
        ) const -> glm::mat4 {
            if (!enabled) {
                return transform.Matrix();
            }

            auto animated = transform;
            const double sine = std::sin(phase_);
            const double x = radius * std::cos(phase_) / (1.0 + sine * sine);
            animated.position += glm::vec3{
                static_cast<float>(x), height * static_cast<float>(std::sin(2.0 * phase_)), static_cast<float>(x * sine)
            };
            animated.rotationDegrees.y += glm::degrees(static_cast<float>(2.0 * phase_));
            return animated.Matrix();
        }

        bool enabled{false};
        bool playing{false};
        float speed{1.0f};
        float radius{1.0f};
        float height{0.0f};

    private:
        double phase_{0.0};
    };

    class SceneObject final {
    public:
        explicit SceneObject(
            std::string name
        )
            : name{std::move(name)} {}

        auto ModelMatrix() const -> glm::mat4 {
            return animation.ModelMatrix(transform);
        }

        std::string name;
        MeshKind mesh{MeshKind::Cube};
        Transform transform;
        Material material;
        LemniscateAnimation animation;
        bool castsShadow{true};
    };

    enum class Projection {
        Perspective,
        Orthographic
    };

    class Camera final {
    public:
        auto Forward() const -> glm::vec3 {
            const float yaw = glm::radians(yawDegrees);
            const float pitch = glm::radians(pitchDegrees);

            return {std::cos(yaw) * std::cos(pitch), std::sin(pitch), std::sin(yaw) * std::cos(pitch)};
        }

        auto Right() const -> glm::vec3 {
            return glm::normalize(glm::cross(Forward(), Up()));
        }

        auto Up() const -> glm::vec3 {
            const auto direction = Forward();
            const auto baseRight = NormalizedOr(glm::cross(direction, {0, 1, 0}), {1, 0, 0});
            const auto baseUp = glm::cross(baseRight, direction);
            const auto roll = glm::radians(rollDegrees);

            return baseUp * std::cos(roll) + baseRight * std::sin(roll);
        }

        auto Look(
            const glm::vec2 mouseDelta
        ) -> void {
            yawDegrees = std::remainder(yawDegrees + mouseDelta.x * mouseSensitivity, 360.0f);
            pitchDegrees = std::clamp(pitchDegrees - mouseDelta.y * mouseSensitivity, -89.0f, 89.0f);
        }

        auto ViewMatrix() const -> glm::mat4 {
            return glm::lookAt(position, position + Forward(), Up());
        }

        auto ProjectionMatrix(
            const float aspect
        ) const -> glm::mat4 {
            glm::mat4 result;

            if (projection == Projection::Perspective) {
                result = glm::perspectiveRH_ZO(glm::radians(fieldOfViewDegrees), aspect, nearPlane, farPlane);
            } else {
                const float halfHeight = orthographicHeight * 0.5f;

                result = glm::orthoRH_ZO(
                    -halfHeight * aspect, halfHeight * aspect, -halfHeight, halfHeight, nearPlane, farPlane
                );
            }

            result[1][1] *= -1.0f; // Vulkan framebuffer Y points down.

            return result;
        }

        glm::vec3 position{5.0f, 4.0f, 12.0f};
        float yawDegrees{-112.0f};
        float pitchDegrees{-12.0f};
        float rollDegrees{0.0f};
        float fieldOfViewDegrees{45.0f};
        float orthographicHeight{12.0f};
        float nearPlane{0.1f};
        float farPlane{150.0f};
        float movementSpeed{5.0f};
        float mouseSensitivity{0.18f};
        Projection projection{Projection::Perspective};
    };

    class DirectionalLight final {
    public:
        glm::vec3 direction{-0.5f, -1.0f, -0.3f};
        glm::vec3 color{1.0f, 0.95f, 0.88f};
        float intensity{0.45f};
        bool enabled{true};
    };

    class PointLight final {
    public:
        auto Radiance() const -> glm::vec3 {
            return enabled ? color * intensity : glm::vec3{0.0f};
        }

        glm::vec3 position{-3.0f, 3.0f, 2.0f};
        glm::vec3 color{1.0f, 0.55f, 0.25f};
        float intensity{6.0f};
        bool enabled{true};
    };

    class SpotLight final {
    public:
        auto NormalizedDirection() const -> glm::vec3 {
            return NormalizedOr(direction, {0, -1, 0});
        }

        auto ConeMatrix(
            const float length
        ) const -> glm::mat4 {
            // Unit cone points along local -Z, with its apex at the origin.
            const auto forward = NormalizedDirection();
            const auto reference = std::abs(forward.y) > 0.99f ? glm::vec3{1, 0, 0} : glm::vec3{0, 1, 0};
            const auto right = glm::normalize(glm::cross(forward, reference));
            const auto up = glm::cross(right, forward);

            glm::mat4 basis{1.0f};
            basis[0] = glm::vec4{right, 0};
            basis[1] = glm::vec4{up, 0};
            basis[2] = glm::vec4{-forward, 0};
            basis[3] = glm::vec4{emitter.position, 1};

            const float radius = length * std::tan(glm::radians(outerAngleDegrees));

            return glm::scale(basis, {radius, radius, length});
        }

        PointLight emitter;
        glm::vec3 direction{0.0f, -1.0f, -0.5f};
        float innerAngleDegrees{18.0f};
        float outerAngleDegrees{28.0f};
    };

    class ShadowSettings {
    public:
        auto DirectionalMatrix(
            const DirectionalLight &light
        ) const -> glm::mat4 {
            const auto direction = NormalizedOr(light.direction, {0, -1, 0});
            const float distance = (nearPlane + farPlane) * 0.5f;
            const auto position = center - direction * distance;
            const float halfSize = orthographicSize * 0.5f;

            auto projection = glm::orthoRH_ZO(-halfSize, halfSize, -halfSize, halfSize, nearPlane, farPlane);
            projection[1][1] *= -1.0f;

            return projection * LightView(position, direction);
        }

        auto SpotMatrix(
            const SpotLight &light
        ) const -> glm::mat4 {
            const float fieldOfView = glm::radians(2.0f * light.outerAngleDegrees);
            auto projection = glm::perspectiveRH_ZO(fieldOfView, 1.0f, nearPlane, farPlane);
            projection[1][1] *= -1.0f;

            return projection * LightView(light.emitter.position, light.NormalizedDirection());
        }

        bool enabled{true};
        bool directionalEnabled{true};
        bool spotEnabled{true};
        std::size_t spotLightIndex{0};
        glm::vec3 center{0.0f};
        float orthographicSize{26.0f};
        float nearPlane{0.1f};
        float farPlane{60.0f};
        float depthBias{1.25f};
        float slopeBias{1.75f};

    private:
        static auto LightView(
            const glm::vec3 position,
            const glm::vec3 direction
        ) -> glm::mat4 {
            const auto up = std::abs(direction.y) > 0.99f ? glm::vec3{1, 0, 0} : glm::vec3{0, 1, 0};

            return glm::lookAtRH(position, position + direction, up);
        }
    };

    class Scene final {
    public:
        Scene() {
            SceneObject first{"Cube 1"};
            first.transform.position = {-1.6f, 0.0f, 0.0f};
            first.material.preset = MaterialPreset::Brick;
            objects.push_back(std::move(first));

            SceneObject second{"Cube 2"};
            second.transform.position = {1.6f, 0.0f, -1.0f};
            second.transform.rotationDegrees = {0, 25, 0};
            second.material.preset = MaterialPreset::Circuit;
            second.material.sampling.animated = true;
            objects.push_back(std::move(second));

            SceneObject floor{"Floor"};
            floor.mesh = MeshKind::Plane;
            floor.castsShadow = false;
            floor.transform.position = {0, -1.08f, 0};
            floor.transform.scale = {9.0f, 1.0f, 9.0f};
            floor.material.preset = MaterialPreset::Stone;
            floor.material.sampling.tiling = {9.0f, 9.0f};
            floor.material.vertexColorWeight = 0.0f;
            floor.material.shininess = 24.0f;
            floor.material.specular = glm::vec3{0.15f};
            objects.push_back(std::move(floor));

            pointLights.emplace_back(PointLight{});
            PointLight blue;
            blue.position = {3, 2, -1};
            blue.color = {0.3f, 0.55f, 1.0f};
            blue.intensity = 5.0f;
            pointLights.push_back(blue);

            SpotLight spot;
            spot.emitter.position = {0, 5, 3};
            spot.emitter.color = {0.8f, 0.9f, 1.0f};
            spot.emitter.intensity = 12.0f;
            spotLights.push_back(spot);
        }

        auto Advance(
            const double deltaSeconds
        ) -> void {
            if (std::isfinite(deltaSeconds) && deltaSeconds > 0.0) {
                textureTime_ = std::fmod(textureTime_ + deltaSeconds, 4096.0);
            }
            for (auto &object : objects) {
                object.animation.Advance(deltaSeconds);
            }
        }

        auto TextureTime() const -> float {
            return static_cast<float>(textureTime_);
        }

        Camera camera;
        std::vector<SceneObject> objects;
        DirectionalLight directionalLight;
        std::vector<PointLight> pointLights;
        std::vector<SpotLight> spotLights;
        ShadowSettings shadows;
        glm::vec3 ambientColor{0.65f, 0.75f, 1.0f};
        float ambientIntensity{0.12f};
        bool lightingEnabled{true};
        bool showLightMarkers{true};
        bool showSpotCones{true};
        float coneLength{3.0f};

    private:
        double textureTime_{0.0};
    };

} // namespace cg
