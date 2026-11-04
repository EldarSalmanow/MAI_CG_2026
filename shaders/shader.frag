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
    // Light markers and spotlight outlines keep their constant color (Lab 2).
    if (object.unlit > 0.5) {
        outColor = vec4(object.emission, 1.0);
        return;
    }

    Material material = sampleMaterial(textureUV, vertexColor);
    vec3 color = material.albedo;
    if (scene.lightingEnabled != 0u) {
        Surface surface = Surface(worldPosition,
                                  safeNormalize(worldNormal),
                                  safeNormalize(scene.cameraPosition.xyz - worldPosition));
        color = evaluateLighting(surface, material);
    }

    // Emission bypasses Blinn-Phong and remains visible without lights (Lab 3).
    outColor = vec4(color + material.emission, 1.0);
}
