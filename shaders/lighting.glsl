#ifndef CG_LIGHTING_GLSL
#define CG_LIGHTING_GLSL

#include "material.glsl"

struct PointLight {
    vec4 position;
    vec4 radiance;
};

struct SpotLight {
    vec4 position;
    vec4 radiance;
    vec4 direction;
    float innerConeCosine;
    float outerConeCosine;
    vec2 reserved;
};

layout(std430, set = 0, binding = 1) readonly buffer PointLights {
    PointLight values[];
} pointLights;

layout(std430, set = 0, binding = 2) readonly buffer SpotLights {
    SpotLight values[];
} spotLights;

struct Surface {
    vec3 position;
    vec3 normal;
    vec3 viewDirection;
};

struct LightSample {
    vec3 direction;
    vec3 radiance;
};

vec3 safeNormalize(
    vec3 value
) {
    const float minimumLengthSquared = 1e-12;

    return value * inversesqrt(max(dot(value, value), minimumLengthSquared));
}

vec3 blinnPhong(
    Surface surface,
    Material material,
    LightSample light
) {
    float diffuse = max(dot(surface.normal, light.direction), 0.0);
    if (diffuse <= 0.0) {
        return vec3(0.0);
    }

    vec3 halfway = safeNormalize(light.direction + surface.viewDirection);
    float specular = pow(max(dot(surface.normal, halfway), 0.0), material.shininess);

    return light.radiance * (material.albedo * diffuse + material.specular * specular);
}

LightSample sampleLocalLight(
    vec3 offset,
    vec3 radiance
) {
    const float minimumDistanceSquared = 0.01;
    float distanceSquared = max(dot(offset, offset), minimumDistanceSquared);

    return LightSample(safeNormalize(offset), radiance / distanceSquared);
}

float spotlightAttenuation(
    float cosine,
    float innerCosine,
    float outerCosine
) {
    const float coneEpsilon = 1e-6;
    if (innerCosine - outerCosine < coneEpsilon) {
        return step(outerCosine, cosine);
    }

    return smoothstep(outerCosine, innerCosine, cosine);
}

LightSample sampleSpotLight(
    SpotLight light,
    vec3 surfacePosition
) {
    vec3 offset = light.position.xyz - surfacePosition;
    float cosine = dot(-safeNormalize(offset), light.direction.xyz);
    float weight = spotlightAttenuation(cosine, light.innerConeCosine, light.outerConeCosine);

    return sampleLocalLight(offset, light.radiance.rgb * weight);
}

vec3 evaluateLighting(
    Surface surface,
    Material material
) {
    vec3 color = scene.ambient.rgb * material.albedo;

    LightSample directional = LightSample(-scene.lightDirection.xyz, scene.directionalRadiance.rgb);
    color += blinnPhong(surface, material, directional);

    for (uint i = 0u; i < scene.pointLightCount; ++i) {
        PointLight light = pointLights.values[i];
        LightSample incident = sampleLocalLight(light.position.xyz - surface.position, light.radiance.rgb);

        color += blinnPhong(surface, material, incident);
    }

    for (uint i = 0u; i < scene.spotLightCount; ++i) {
        LightSample incident = sampleSpotLight(spotLights.values[i], surface.position);

        color += blinnPhong(surface, material, incident);
    }

    return color;
}

#endif
