#include "application.hpp"
#include "cg/application.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <exception>
#include <iostream>
#include <memory>

namespace {

std::unique_ptr<cg::Application> laboratory;

} // namespace

namespace application {

auto Initialize() -> bool {
    try {
        laboratory = std::make_unique<cg::Application>(graphics::internal::context);

        if (auto *window = static_cast<GLFWwindow *>(ImGui::GetMainViewport()->PlatformHandle)) {
            glfwSetWindowTitle(window, "Computer Graphics - Lab 2");
        }

        return true;
    } catch (const std::exception& error) {
        std::cerr << "[ERROR] Application initialization failed: " << error.what() << std::endl;

        return false;
    }
}

auto Shutdown() -> void {
    if (!laboratory) {
        return;
    }

    vkDeviceWaitIdle(graphics::internal::context.device);
    laboratory.reset();
}

auto Update(const double time) -> void {
    laboratory->Update(time);
}

auto Render(const graphics::internal::FrameData& frame) -> void {
    laboratory->Render(frame);
}

} // namespace application