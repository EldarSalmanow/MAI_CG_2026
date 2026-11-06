#pragma once

#include <glm/glm.hpp>

#include <array>
#include <stdexcept>
#include <string_view>

namespace cg {

    enum class MaterialPreset : std::size_t {
        Solid,
        Brick,
        Circuit,
        Stone
    };

    class MaterialDefinition {
    public:
        MaterialPreset preset;
        std::string_view name;
        std::string_view albedo;
        std::string_view specular;
        std::string_view emissive;
    };

    inline constexpr std::array MaterialDefinitions{
        MaterialDefinition{
            .preset = MaterialPreset::Solid,
            .name = "Solid color",
            .albedo = "white.png",
            .specular = "white.png",
            .emissive = ""
        },
        MaterialDefinition{
            .preset = MaterialPreset::Brick,
            .name = "Terracotta brick",
            .albedo = "brick_albedo.png",
            .specular = "brick_specular.png",
            .emissive = ""
        },
        MaterialDefinition{
            .preset = MaterialPreset::Circuit,
            .name = "Luminous circuit",
            .albedo = "circuit_albedo.png",
            .specular = "circuit_specular.png",
            .emissive = "circuit_emissive.png"
        },
        MaterialDefinition{
            .preset = MaterialPreset::Stone,
            .name = "Stone tiles",
            .albedo = "stone_albedo.png",
            .specular = "",
            .emissive = ""
        }
    };

    constexpr auto MaterialIndex(
        const MaterialPreset preset
    ) -> std::size_t {
        for (std::size_t i = 0; i < MaterialDefinitions.size(); ++i) {
            if (MaterialDefinitions[i].preset == preset) {
                return i;
            }
        }

        throw std::invalid_argument{"[ERROR] Unknown material preset!"};
    }

    class SamplingSettings {
    public:
        glm::vec2 tiling{1.0f};
        float speed{0.35f};
        float blendWeight{0.5f};
        float emissionStrength{2.0f};
        bool animated{false};
    };

    class Material {
    public:
        MaterialPreset preset{MaterialPreset::Solid};
        SamplingSettings sampling;
        glm::vec3 albedo{1.0f};
        glm::vec3 specular{0.5f};
        float shininess{48.0f};
        float vertexColorWeight{0.0f};
    };

} // namespace cg
