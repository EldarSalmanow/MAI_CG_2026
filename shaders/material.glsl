#ifndef CG_MATERIAL_GLSL
#define CG_MATERIAL_GLSL

#include "scene_interface.glsl"

layout(set = 1, binding = 1) uniform sampler2D albedoMap;
layout(set = 1, binding = 2) uniform sampler2D specularMap;
layout(set = 1, binding = 3) uniform sampler2D emissiveMap;

layout(push_constant) uniform SamplingParameters {
    vec2 tiling;
    float time;
    float speed;
    float blendWeight;
    uint animationEnabled;
    float emissionStrength;
    uint reserved;
} sampling;

struct Material {
    vec3 albedo;
    vec3 specular;
    vec3 emissiveColor;
    float shininess;
};

Material sampleTextures(
    vec2 uv
) {
    return Material(
        texture(albedoMap, uv).rgb,
        texture(specularMap, uv).rgb,
        texture(emissiveMap, uv).rgb,
        object.shininess
    );
}

Material sampleAnimatedTextures(
    vec2 uv,
    float time
) {
    const vec2 firstVelocity = vec2(0.1, 0.0);
    const vec2 secondVelocity = vec2(0.0, -0.1);
    const float secondScale = 2.0;
    const vec3 pulseTint = vec3(0.5, 0.8, 1.0);

    Material first = sampleTextures(uv + firstVelocity * time);
    Material second = sampleTextures(uv * secondScale + secondVelocity * time);

    float pulse = 0.5 + 0.5 * sin(time);
    vec3 modulation = mix(vec3(1.0), pulseTint, pulse);

    return Material(
        mix(first.albedo, second.albedo, sampling.blendWeight) * modulation,
        mix(first.specular, second.specular, sampling.blendWeight),
        mix(first.emissiveColor, second.emissiveColor, sampling.blendWeight),
        object.shininess
    );
}

Material sampleMaterial(
    vec2 uv,
    vec3 vertexColor
) {
    uv *= sampling.tiling;

    Material material;
    if (sampling.animationEnabled != 0u) {
        material = sampleAnimatedTextures(uv, sampling.time * sampling.speed);
    } else {
        material = sampleTextures(uv);
    }

    material.albedo *= object.albedo * mix(vec3(1.0), vertexColor, object.vertexColorWeight);
    material.specular *= object.specular;
    material.emissiveColor *= sampling.emissionStrength;

    return material;
}

#endif
