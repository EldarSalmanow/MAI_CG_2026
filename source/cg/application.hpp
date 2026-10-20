#pragma once

#include "editor.hpp"
#include "renderer.hpp"

#include <algorithm>
#include <optional>


namespace cg {

class Application {
public:
    explicit Application(
        const graphics::internal::Context &context
    ) : renderer_{context} {}

public:
    auto Update(
        const double time
    ) -> void {
        const double delta = previousTime_ && std::isfinite(time) ? std::clamp(time - *previousTime_, 0.0, 0.1) : 0.0;

        previousTime_ = std::isfinite(time) ? std::optional{time} : std::nullopt;

        editor_.Draw(scene_, renderer_.PointLightLimit(), renderer_.SpotLightLimit());
        editor_.Navigate(scene_.camera, delta);

        scene_.Advance(delta);
    }

    auto Render(
        const graphics::internal::FrameData &frame
    ) -> void {
        renderer_.Render(scene_, frame);
    }

private:
    Scene scene_;
    SceneEditor editor_;
    SceneRenderer renderer_;
    std::optional<double> previousTime_;
};

} // namespace cg
