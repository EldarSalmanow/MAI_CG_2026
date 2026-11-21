#version 450
#extension GL_GOOGLE_include_directive : require

#include "scene.glsl"

layout(location = 0) in vec3 inPosition;

layout(push_constant) uniform ShadowPushConstants {
    layout(offset = 32) mat4 lightViewProjection;
} shadow;

void main() {
    gl_Position = shadow.lightViewProjection * object.model * vec4(inPosition, 1.0);
}
