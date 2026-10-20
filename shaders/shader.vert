#version 450
#extension GL_GOOGLE_include_directive : require
#include "scene_interface.glsl"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;

layout(location = 0) out vec3 worldPosition;
layout(location = 1) out vec3 worldNormal;
layout(location = 2) out vec3 vertexColor;

void main() {
    vec4 world = object.model * vec4(inPosition, 1.0);
    worldPosition = world.xyz;
    worldNormal = mat3(object.normalMatrix) * inNormal;
    vertexColor = inColor;
    gl_Position = scene.viewProjection * world;
}