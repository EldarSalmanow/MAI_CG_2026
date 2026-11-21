#ifndef CG_SHADOW_GLSL
#define CG_SHADOW_GLSL

#include "scene.glsl"

layout(set = 0, binding = 3) uniform sampler2DShadow directionalShadowMap;
layout(set = 0, binding = 4) uniform sampler2DShadow spotShadowMap;

float shadowVisibility(
    sampler2DShadow shadowMap,
    mat4 lightViewProjection,
    vec3 worldPosition
) {
    vec4 lightClipPosition = lightViewProjection * vec4(worldPosition, 1.0);
    if (lightClipPosition.w <= 0.0) {
        return 1.0;
    }

    vec3 lightNdcPosition = lightClipPosition.xyz / lightClipPosition.w;
    vec2 uv = lightNdcPosition.xy * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))
        || lightNdcPosition.z < 0.0 || lightNdcPosition.z >= 1.0) {
        return 1.0;
    }

    return textureLod(shadowMap, vec3(uv, lightNdcPosition.z), 0.0);
}

float directionalShadowVisibility(
    vec3 worldPosition
) {
    if (scene.directionalShadowEnabled == 0u) {
        return 1.0;
    }

    return shadowVisibility(directionalShadowMap, scene.directionalLightViewProjection, worldPosition);
}

float spotShadowVisibility(
    uint spotLightIndex,
    vec3 worldPosition
) {
    // if (scene.spotShadowEnabled == 0u || spotLightIndex != scene.shadowSpotLightIndex) {
    if (scene.spotShadowEnabled == 0u) {
        return 1.0;
    }

    return shadowVisibility(spotShadowMap, scene.spotLightViewProjection, worldPosition);
}

#endif
