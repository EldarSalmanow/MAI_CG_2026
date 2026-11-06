#pragma once

#include <memory>

namespace graphics::internal {
    struct Context;
    struct FrameData;
} // namespace graphics::internal

namespace cg {

    class Scene;

    class SceneRenderer final {
    public:
        explicit SceneRenderer(
            const graphics::internal::Context &context
        );

        SceneRenderer(
            const SceneRenderer &other
        ) = delete;

    public:
        ~SceneRenderer();

    public:
        auto Render(
            const Scene &scene,
            const graphics::internal::FrameData &frame
        ) -> void;

        auto PointLightLimit() const noexcept -> std::size_t;

        auto SpotLightLimit() const noexcept -> std::size_t;

    public:
        auto operator=(
            const SceneRenderer &other
        ) -> SceneRenderer & = delete;

    private:
        class Implementation;
        std::unique_ptr<Implementation> implementation_;
    };

} // namespace cg
