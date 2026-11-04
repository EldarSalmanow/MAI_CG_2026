#version 450
#extension GL_GOOGLE_include_directive : require
#include "scene_interface.glsl"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inUV;

layout(location = 0) out vec3 worldPosition;
layout(location = 1) out vec3 worldNormal;
layout(location = 2) out vec3 vertexColor;
layout(location = 3) out vec2 textureUV;

void main() {
    vec4 worldVertex = object.model * vec4(inPosition, 1.0);
    gl_Position = scene.viewProjection * worldVertex;
    worldPosition = worldVertex.xyz;
    // Normalization happens per fragment after interpolation.
    worldNormal = mat3(object.normalMatrix) * inNormal;
    vertexColor = inColor;
    textureUV = inUV;
}
