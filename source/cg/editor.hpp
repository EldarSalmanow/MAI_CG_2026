#pragma once

#include "scene.hpp"

#include <imgui.h>
#include <glm/gtc/type_ptr.hpp>

#include <string>


namespace cg {

    class SceneEditor {
    public:
        auto Draw(
            Scene &scene,
            const std::size_t pointLimit,
            const std::size_t spotLimit
        ) -> void {
            ImGui::SetNextWindowPos({16, 16}, ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize({380, 640}, ImGuiCond_FirstUseEver);

            if (ImGui::Begin("Computer Graphics - Laboratory Work 2")) {
                if (ImGui::BeginTabBar("Scene editor")) {
                    if (ImGui::BeginTabItem("Scene")) {
                        DrawScene(scene);

                        ImGui::EndTabItem();
                    }

                    if (ImGui::BeginTabItem("Objects")) {
                        DrawObjects(scene);

                        ImGui::EndTabItem();
                    }

                    if (ImGui::BeginTabItem("Camera")) {
                        DrawCamera(scene.camera);

                        ImGui::EndTabItem();
                    }

                    if (ImGui::BeginTabItem("Lights")) {
                        DrawLights(scene, pointLimit, spotLimit);

                        ImGui::EndTabItem();
                    }

                    ImGui::EndTabBar();
                }
            }

            ImGui::End();
        }

        auto Navigate(
            Camera &camera,
            const double deltaSeconds
        ) const -> void {
            const auto &io = ImGui::GetIO();

            if (!io.WantCaptureMouse && ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f)) {
                camera.Look({io.MouseDelta.x, io.MouseDelta.y});
            }

            if (io.WantCaptureKeyboard || io.WantTextInput) {
                return;
            }

            glm::vec3 movement{0.0f};

            if (ImGui::IsKeyDown(ImGuiKey_W)) {
                movement += camera.Forward();
            }

            if (ImGui::IsKeyDown(ImGuiKey_S)) {
                movement -= camera.Forward();
            }

            if (ImGui::IsKeyDown(ImGuiKey_D)) {
                movement += camera.Right();
            }

            if (ImGui::IsKeyDown(ImGuiKey_A)) {
                movement -= camera.Right();
            }

            if (ImGui::IsKeyDown(ImGuiKey_E)) {
                movement += glm::vec3{0, 1, 0};
            }

            if (ImGui::IsKeyDown(ImGuiKey_Q)) {
                movement -= glm::vec3{0, 1, 0};
            }

            const float multiplier = io.KeyShift ? 3.0f : 1.0f;

            camera.position += normalizedOr(movement, glm::vec3{0.0f})
                             * camera.movementSpeed
                             * multiplier
                             * static_cast<float>(deltaSeconds);
        }

    private:
        // Ctrl-click text entry must obey the same bounds as dragging the slider.
        static auto SliderFloat(
            const char *label,
            float *value,
            const float minimum,
            const float maximum,
            const char *format = "%.3f",
            const ImGuiSliderFlags flags = 0
        ) -> bool {
            return ImGui::SliderFloat(label, value, minimum, maximum, format, flags | ImGuiSliderFlags_AlwaysClamp);
        }

        static auto SliderFloat3(
            const char *label,
            float *value,
            const float minimum,
            const float maximum
        ) -> bool {
            return ImGui::SliderFloat3(label, value, minimum, maximum, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        }

        static auto DrawProjection(
            Camera &camera
        ) -> void {
            if (ImGui::RadioButton("Perspective", camera.projection == Projection::Perspective)) {
                camera.projection = Projection::Perspective;
            }

            ImGui::SameLine();

            if (ImGui::RadioButton("Orthographic", camera.projection == Projection::Orthographic)) {
                camera.projection = Projection::Orthographic;
            }
        }

        // TODO:
        static auto DrawScene(
            Scene &scene
        ) -> void {
            DrawProjection(scene.camera);
            ImGui::Spacing();
            ImGui::Checkbox("Lighting", &scene.lightingEnabled);
            ImGui::TextWrapped("Disable lighting to inspect the original vertex colors and object tint.");
            ImGui::SeparatorText("Ambient light");
            ImGui::ColorEdit3("Ambient color", glm::value_ptr(scene.ambientColor));
            SliderFloat("Ambient intensity", &scene.ambientIntensity, 0.0f, 1.0f);
            ImGui::SeparatorText("Light visualization");
            ImGui::Checkbox("Show light markers", &scene.showLightMarkers);
            ImGui::Checkbox("Show spotlight cones", &scene.showSpotCones);
            SliderFloat("Cone length", &scene.coneLength, 0.2f, 15.0f);
            ImGui::SeparatorText("Navigation");
            ImGui::TextWrapped(
                               "WASD: move / Q,E: down,up / Shift: faster\nRight mouse drag outside this window: look");
            ImGui::Text("%.1f FPS", static_cast<double>(ImGui::GetIO().Framerate));
        }

        auto DrawObjects(
            Scene &scene
        ) -> void {
            if (scene.objects.empty()) {
                ImGui::TextUnformatted("No scene objects");
                return;
            }
            selectedObject_ = std::min(selectedObject_, scene.objects.size() - 1);
            if (ImGui::BeginCombo("Object", scene.objects[selectedObject_].name.c_str())) {
                for (std::size_t i = 0; i < scene.objects.size(); ++i) {
                    if (ImGui::Selectable(scene.objects[i].name.c_str(), selectedObject_ == i)) {
                        selectedObject_ = i;
                    }
                }
                ImGui::EndCombo();
            }
            auto &object = scene.objects[selectedObject_];
            ImGui::SeparatorText("Transform");
            ImGui::DragFloat3("Position", glm::value_ptr(object.transform.position), 0.05f);
            SliderFloat3("Rotation", glm::value_ptr(object.transform.rotationDegrees), -180.0f, 180.0f);
            ImGui::DragFloat3("Scale", glm::value_ptr(object.transform.scale), 0.02f, 0.05f, 20.0f, "%.2f",
                              ImGuiSliderFlags_AlwaysClamp);
            ImGui::SeparatorText("Material");
            auto &material = object.material;
            ImGui::ColorEdit3("Albedo", glm::value_ptr(material.albedo));
            ImGui::ColorEdit3("Specular", glm::value_ptr(material.specular));
            SliderFloat("Shininess", &material.shininess, 1.0f, 256.0f, "%.0f");
            SliderFloat("Vertex colors", &material.vertexColorWeight, 0.0f, 1.0f);
            ImGui::SeparatorText("Lemniscate animation");
            auto &animation = object.animation;
            ImGui::Checkbox("Enable animation", &animation.enabled);
            ImGui::BeginDisabled(!animation.enabled);
            if (ImGui::Button(animation.playing ? "Pause" : "Play")) {
                animation.playing = !animation.playing;
            }
            ImGui::SameLine();
            if (ImGui::Button("Restart")) {
                animation.Restart();
            }
            SliderFloat("Speed", &animation.speed, 0.1f, 5.0f);
            SliderFloat("Radius", &animation.radius, 0.1f, 10.0f);
            SliderFloat("Vertical amplitude", &animation.height, 0.0f, 5.0f);
            ImGui::EndDisabled();
        }

        static auto DrawCamera(
            Camera &camera
        ) -> void {
            DrawProjection(camera);
            ImGui::DragFloat3("Position", glm::value_ptr(camera.position), 0.05f);
            SliderFloat("Yaw", &camera.yawDegrees, -180.0f, 180.0f);
            SliderFloat("Pitch", &camera.pitchDegrees, -89.0f, 89.0f);
            SliderFloat("Roll", &camera.rollDegrees, -180.0f, 180.0f);
            SliderFloat("Field of view", &camera.fieldOfViewDegrees, 15.0f, 110.0f, "%.1f deg");
            SliderFloat("Ortho height", &camera.orthographicHeight, 1.0f, 60.0f);
            ImGui::DragFloat("Near plane", &camera.nearPlane, 0.01f, 0.01f, camera.farPlane - 0.1f, "%.2f",
                             ImGuiSliderFlags_AlwaysClamp);
            ImGui::DragFloat("Far plane", &camera.farPlane, 1.0f, camera.nearPlane + 0.1f, 1000.0f, "%.1f",
                             ImGuiSliderFlags_AlwaysClamp);
            SliderFloat("Movement speed", &camera.movementSpeed, 0.2f, 30.0f);
            SliderFloat("Mouse sensitivity", &camera.mouseSensitivity, 0.02f, 1.0f);
            if (ImGui::Button("Reset camera")) {
                camera = Camera{};
            }
        }

        static auto EditEmitter(
            PointLight &light
        ) -> void {
            ImGui::Checkbox("Enabled", &light.enabled);
            ImGui::DragFloat3("Position", glm::value_ptr(light.position), 0.05f);
            ImGui::ColorEdit3("Color", glm::value_ptr(light.color));
            ImGui::DragFloat("Intensity", &light.intensity, 0.25f, 0.0f, 1000.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        }

        static auto EditLight(
            PointLight &light
        ) -> void {
            EditEmitter(light);
        }

        static auto EditLight(
            SpotLight &light
        ) -> void {
            EditEmitter(light.emitter);
            SliderFloat3("Direction", glm::value_ptr(light.direction), -1.0f, 1.0f);
            SliderFloat("Inner half-angle", &light.innerAngleDegrees, 0.0f, light.outerAngleDegrees, "%.1f deg",
                        ImGuiSliderFlags_AlwaysClamp);
            SliderFloat("Outer half-angle", &light.outerAngleDegrees, std::max(0.1f, light.innerAngleDegrees),
                        85.0f, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
            ImGui::TextWrapped("Equal angles produce a hard edge; separated angles produce a soft edge.");
        }

        template <class Light>
        static auto EditCollection(
            const char *name,
            std::vector<Light> &lights,
            std::size_t &selection,
            const std::size_t limit
        ) -> void {
            ImGui::PushID(name);

            ImGui::BeginDisabled(lights.size() >= limit);

            if (ImGui::Button("Add")) {
                lights.emplace_back();

                selection = lights.size() - 1;
            }

            ImGui::EndDisabled();

            ImGui::SameLine();

            ImGui::BeginDisabled(lights.empty());

            if (ImGui::Button("Remove")) {
                selection = std::min(selection, lights.size() - 1);

                lights.erase(lights.begin() + static_cast<std::ptrdiff_t>(selection));
            }

            ImGui::EndDisabled();

            ImGui::SameLine();

            ImGui::Text("Count: %zu", lights.size());

            if (!lights.empty()) {
                selection = std::min(selection, lights.size() - 1);

                const auto selectedName = std::string{name} + " " + std::to_string(selection + 1);

                if (ImGui::BeginCombo("Source", selectedName.c_str())) {
                    for (std::size_t i = 0; i < lights.size(); ++i) {
                        const auto label = std::string{name} + " " + std::to_string(i + 1);

                        if (ImGui::Selectable(label.c_str(), selection == i)) {
                            selection = i;
                        }
                    }

                    ImGui::EndCombo();
                }

                EditLight(lights[selection]);
            }

            ImGui::PopID();
        }

        auto DrawLights(
            Scene &scene,
            const std::size_t pointLimit,
            const std::size_t spotLimit
        ) -> void {
            if (ImGui::CollapsingHeader("Directional light", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::PushID("Directional");
                auto &light = scene.directionalLight;
                ImGui::Checkbox("Enabled", &light.enabled);
                SliderFloat3("Direction", glm::value_ptr(light.direction), -1.0f, 1.0f);
                ImGui::ColorEdit3("Color", glm::value_ptr(light.color));
                SliderFloat("Intensity", &light.intensity, 0.0f, 5.0f);
                ImGui::PopID();
            }

            if (ImGui::CollapsingHeader("Point lights", ImGuiTreeNodeFlags_DefaultOpen)) {
                EditCollection("Point", scene.pointLights, selectedPoint_, pointLimit);
            }

            if (ImGui::CollapsingHeader("Spot lights", ImGuiTreeNodeFlags_DefaultOpen)) {
                EditCollection("Spot", scene.spotLights, selectedSpot_, spotLimit);
            }
        }

        std::size_t selectedObject_ = 0;
        std::size_t selectedPoint_  = 0;
        std::size_t selectedSpot_   = 0;
    };
} // namespace cg
