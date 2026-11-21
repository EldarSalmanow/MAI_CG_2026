#ifndef CG_LIGHTING_GLSL
#define CG_LIGHTING_GLSL

#include "scene.glsl"
#include "material.glsl"
#include "shadow.glsl"

struct Surface {
    vec3 worldPosition;
    vec3 worldNormal;
    vec3 directionToCamera;
};

struct LightSample {
    vec3 directionToLight;
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
    float diffuse = max(dot(surface.worldNormal, light.directionToLight), 0.0);
    if (diffuse <= 0.0) {
        return vec3(0.0);
    }

    vec3 halfway = safeNormalize(light.directionToLight + surface.directionToCamera);
    float specular = pow(max(dot(surface.worldNormal, halfway), 0.0), material.shininess);

    return light.radiance * (material.albedo * diffuse + material.specular * specular);
}

LightSample sampleLocalLight(
    vec3 toLight,
    vec3 radiance
) {
    const float minimumDistanceSquared = 0.01;
    float distanceSquared = max(dot(toLight, toLight), minimumDistanceSquared);

    return LightSample(safeNormalize(toLight), radiance / distanceSquared);
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
    SpotLightData light,
    vec3 worldPosition
) {
    vec3 toLight = light.position.xyz - worldPosition;
    float cosine = dot(-safeNormalize(toLight), light.direction.xyz);
    float weight = spotlightAttenuation(cosine, light.innerConeCosine, light.outerConeCosine);

    return sampleLocalLight(toLight, light.radiance.rgb * weight);
}

vec3 evaluateLighting(
    Surface surface,
    Material material
) {
    vec3 color = scene.ambientRadiance.rgb * material.albedo;

    LightSample directional = LightSample(-scene.directionalLightDirection.xyz, scene.directionalLightRadiance.rgb);
    directional.radiance *= directionalShadowVisibility(surface.worldPosition);
    color += blinnPhong(surface, material, directional);

    for (uint i = 0u; i < scene.pointLightCount; ++i) {
        PointLightData light = pointLights.values[i];
        LightSample incident = sampleLocalLight(light.position.xyz - surface.worldPosition, light.radiance.rgb);

        color += blinnPhong(surface, material, incident);
    }

    for (uint i = 0u; i < scene.spotLightCount; ++i) {
        SpotLightData light = spotLights.values[i];
        LightSample incident = sampleSpotLight(light, surface.worldPosition);
        incident.radiance *= spotShadowVisibility(i, surface.worldPosition);

        color += blinnPhong(surface, material, incident);
    }

    return color;
}

#endif
