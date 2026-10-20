#pragma once

#include "scene.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace cg {

class Vertex {
public:
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 color;
};

class Mesh {
public:
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
};

class MeshFactory {
public:
    static auto Cube() -> Mesh {
        constexpr std::array<glm::vec3, 8> corners {
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
        constexpr std::array<std::array<std::uint32_t, 4>, 6> faces {
            {
                {0, 3, 2, 1},
                {1, 2, 6, 5},
                {4, 5, 6, 7},
                {0, 4, 7, 3},
                {0, 1, 5, 4},
                {3, 7, 6, 2}
            }
        };

        Mesh mesh;
        mesh.vertices.reserve(24);
        mesh.indices.reserve(36);

        for (const auto &face : faces) {
            const auto normal = glm::normalize(glm::cross(corners[face[1]] - corners[face[0]],
                                                                     corners[face[2]] - corners[face[0]]));

            const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
            for (const auto &corner : face) {
                const auto position = corners[corner];

                mesh.vertices.push_back(Vertex{
                    .position = position,
                    .normal = normal,
                    .color = (position + 1.0f) * 0.5f
                });
            }

            for (const auto &index : {0u, 1u, 2u, 2u, 3u, 0u}) {
                mesh.indices.push_back(base + index);
            }
        }

        return mesh;
    }

    static auto ConeOutline() -> Mesh {
        constexpr std::uint32_t segments = 48;

        Mesh mesh;
        mesh.vertices.push_back(Vertex{
            .position = {0, 0, 0},
            .normal = {0, 1, 0},
            .color = {1, 1, 1}
        });
        mesh.vertices.push_back(Vertex{
            .position = {0, 0, -1},
            .normal = {0, 1, 0},
            .color = {1, 1, 1}
        });

        for (std::uint32_t i = 0; i < segments; ++i) {
            const float angle = 2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / segments;

            mesh.vertices.push_back(Vertex{
                .position = {std::cos(angle), std::sin(angle), -1},
                .normal = {0, 1, 0},
                .color = {1, 1, 1}
            });
            mesh.indices.insert(mesh.indices.end(), {2 + i, 2 + (i + 1) % segments});

            if (i % (segments / 4) == 0) {
                mesh.indices.insert(mesh.indices.end(), {0, 2 + i});
            }
        }

        mesh.indices.insert(mesh.indices.end(), {0, 1});

        return mesh;
    }
};

} // namespace cg
