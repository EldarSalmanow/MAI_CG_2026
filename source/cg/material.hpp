#pragma once

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <string_view>

namespace cg {

enum class MaterialPreset : std::size_t {
    Solid,
    Brick,
    Circuit,
    Stone
};

class MaterialDefinition final {
public:
    std::string_view name;
    std::string_view albedo;
    std::string_view specular;
    std::string_view emissive;
};

// An empty optional map resolves to the shared black texture.
inline constexpr std::array MaterialDefinitions{
    MaterialDefinition{"Solid color", "white.png", "white.png", ""},
    MaterialDefinition{"Terracotta brick", "brick_albedo.png", "brick_specular.png", ""},
    MaterialDefinition{"Luminous circuit", "circuit_albedo.png", "circuit_specular.png", "circuit_emissive.png"},
    MaterialDefinition{"Stone tiles", "stone_albedo.png", "", ""}
};

class SamplingSettings final {
public:
    glm::vec2 tiling{1.0f};
    float speed{0.35f};
    float distortion{0.2f};
    float emissionStrength{2.0f};
    bool animated{false};
};

} // namespace cg
