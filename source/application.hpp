#pragma once

#include "graphics_internal.hpp"

namespace application {

auto Initialize() -> bool;

auto Shutdown() -> void;

auto Update(double time) -> void;

auto Render(const graphics::internal::FrameData &frame_data) -> void;

} // namespace application