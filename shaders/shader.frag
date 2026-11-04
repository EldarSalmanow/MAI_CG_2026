#version 450
#extension GL_GOOGLE_include_directive: require
#include "scene_interface.glsl"

layout(set = 1, binding = 1) uniform sampler2D albedoMap;
layout(set = 1, binding = 2) uniform sampler2D specularMap;
layout(set = 1, binding = 3) uniform sampler2D emissiveMap;

layout(push_constant) uniform SamplingPushConstants {
    vec4 uvTime;  // repeat U/V, elapsed time, speed
    vec4 effects; // distortion, animated flag, emission strength, reserved
} sampling;

struct PointLight {
    vec4 position;
    vec4 radiance;
};

struct SpotLight {
    vec4 position;
    vec4 radiance;
    vec4 direction;
    vec4 coneCosines;
};

layout (std430, set = 0, binding = 1) readonly buffer PointLights {
    PointLight values[];
} pointLights;

layout (std430, set = 0, binding = 2) readonly buffer SpotLights {
    SpotLight values[];
} spotLights;

layout (location = 0) in vec3 worldPosition;
layout (location = 1) in vec3 worldNormal;
layout (location = 2) in vec3 vertexColor;
layout (location = 3) in vec2 textureUV;
layout (location = 0) out vec4 outColor;

vec3 safeNormalize(vec3 value) {
    return value * inversesqrt(max(dot(value, value), 1e-12));
}

vec3 blinnPhong(vec3 normal,
                vec3 viewDirection,
                vec3 lightDirection,
                vec3 radiance,
                vec3 albedo,
                vec3 specularColor) {
    float diffuse = max(dot(normal, lightDirection), 0.0);

    if (diffuse <= 0.0) {
        return vec3(0.0);
    }

    vec3 halfway = safeNormalize(lightDirection + viewDirection);
    float specular = pow(max(dot(normal, halfway), 0.0), object.specular.a);

    return radiance * (albedo * diffuse + specularColor * specular);
}

struct SurfaceSample {
    vec3 albedo;
    vec3 specular;
    vec3 emission;
};

SurfaceSample sampleSurface(vec2 uv, float time) {
    if (sampling.effects.y < 0.5) {
        return SurfaceSample(texture(albedoMap, uv).rgb,
                             texture(specularMap, uv).rgb,
                             texture(emissiveMap, uv).rgb);
    }
    // Two drifting samples of the same image, with independent coordinates.
    vec2 wave = sin(uv.yx * 6.2831853 + vec2(time, -time * 0.73));
    vec2 firstUV = uv + sampling.effects.x * wave + vec2(time * 0.05, 0.0);
    vec2 secondUV = uv * 1.7 - vec2(time * 0.07, time * 0.04);
    float weight = 0.35 + 0.15 * sin(time + uv.x * 6.2831853);
    vec3 modulation = vec3(0.82) + 0.18 * cos(time + vec3(0.0, 2.094, 4.189));
    // All maps share the deformation, keeping highlights and luminous traces aligned.
    return SurfaceSample(
        mix(texture(albedoMap, firstUV).rgb, texture(albedoMap, secondUV).rgb, weight) * modulation,
        mix(texture(specularMap, firstUV).rgb, texture(specularMap, secondUV).rgb, weight),
        mix(texture(emissiveMap, firstUV).rgb, texture(emissiveMap, secondUV).rgb, weight));
}

float coneWeight(float cosine, vec2 coneCosines) {
    if (coneCosines.x - coneCosines.y < 1e-6) {
        return step(coneCosines.y, cosine);
    }

    return smoothstep(coneCosines.y, coneCosines.x, cosine);
}

void main() {
    if (object.emission.a > 0.5) {
        outColor = vec4(object.emission.rgb, 1.0);

        return;
    }

    float time = sampling.uvTime.z * sampling.uvTime.w;
    vec2 uv = textureUV * sampling.uvTime.xy;
    SurfaceSample surface = sampleSurface(uv, time);
    vec3 albedo = surface.albedo * object.albedo.rgb
        * mix(vec3(1.0), vertexColor, object.albedo.a);
    vec3 specularColor = object.specular.rgb * surface.specular;
    vec3 emission = surface.emission * sampling.effects.z;

    if (scene.lightCounts.z == 0u) {
        outColor = vec4(albedo + emission, 1.0);

        return;
    }

    vec3 normal = safeNormalize(worldNormal);
    vec3 viewDirection = safeNormalize(scene.cameraPosition.xyz - worldPosition);
    vec3 color = scene.ambient.rgb * albedo;

    color += blinnPhong(normal,
                        viewDirection,
                        -scene.lightDirection.xyz,
                        scene.directionalRadiance.rgb,
                        albedo, specularColor);

    // points
    for (uint i = 0u; i < scene.lightCounts.x; ++i) {
        vec3 offset = pointLights.values[i].position.xyz - worldPosition;
        float distanceSquared = max(dot(offset, offset), 0.01);

        color += blinnPhong(normal,
                            viewDirection,
                            safeNormalize(offset),
                            pointLights.values[i].radiance.rgb / distanceSquared,
                            albedo, specularColor);
    }

    // spots
    for (uint i = 0u; i < scene.lightCounts.y; ++i) {
        vec3 offset = spotLights.values[i].position.xyz - worldPosition;
        float distanceSquared = max(dot(offset, offset), 0.01);
        vec3 lightDirection = safeNormalize(offset);
        float cosine = dot(-lightDirection, spotLights.values[i].direction.xyz);
        float weight = coneWeight(cosine, spotLights.values[i].coneCosines.xy);

        color += blinnPhong(normal,
                            viewDirection,
                            lightDirection,
                            spotLights.values[i].radiance.rgb * weight / distanceSquared,
                            albedo, specularColor);
    }

    outColor = vec4(color + emission, 1.0);
}