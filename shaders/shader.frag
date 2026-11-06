#version 450
#extension GL_GOOGLE_include_directive : require

#include "material.glsl"
#include "lighting.glsl"

layout(location = 0) in vec3 worldPosition;
layout(location = 1) in vec3 worldNormal;
layout(location = 2) in vec3 vertexColor;
layout(location = 3) in vec2 textureUV;
layout(location = 0) out vec4 outColor;

void main() {
    if (object.unlit != 0u) {
        outColor = vec4(object.unlitColor, 1.0);
        return;
    }

    Material material = sampleMaterial(textureUV, vertexColor);
    vec3 color = material.albedo;
    if (scene.lightingEnabled != 0u) {
        Surface surface = Surface(
            worldPosition,
            safeNormalize(worldNormal),
            safeNormalize(scene.cameraPosition.xyz - worldPosition)
        );

        color = evaluateLighting(surface, material);
    }

    outColor = vec4(color + material.emissiveColor, 1.0);
}
