#version 450
#extension GL_GOOGLE_include_directive: require
#include "scene_interface.glsl"

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
layout (location = 0) out vec4 outColor;

vec3 safeNormalize(vec3 value) {
    return value * inversesqrt(max(dot(value, value), 1e-12));
}

vec3 blinnPhong(vec3 normal,
                vec3 viewDirection,
                vec3 lightDirection,
                vec3 radiance,
                vec3 albedo) {
    float diffuse = max(dot(normal, lightDirection), 0.0);

    if (diffuse <= 0.0) {
        return vec3(0.0);
    }

    vec3 halfway = safeNormalize(lightDirection + viewDirection);
    float specular = pow(max(dot(normal, halfway), 0.0), object.specular.a);

    return radiance * (albedo * diffuse + object.specular.rgb * specular);
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

    vec3 albedo = object.albedo.rgb * mix(vec3(1.0), vertexColor, object.albedo.a);

    if (scene.lightCounts.z == 0u) {
        outColor = vec4(albedo, 1.0);

        return;
    }

    vec3 normal = safeNormalize(worldNormal);
    vec3 viewDirection = safeNormalize(scene.cameraPosition.xyz - worldPosition);
    vec3 color = scene.ambient.rgb * albedo;

    color += blinnPhong(normal,
                        viewDirection,
                        -scene.lightDirection.xyz,
                        scene.directionalRadiance.rgb,
                        albedo);

    // points
    for (uint i = 0u; i < scene.lightCounts.x; ++i) {
        vec3 offset = pointLights.values[i].position.xyz - worldPosition;
        float distanceSquared = max(dot(offset, offset), 0.01);

        color += blinnPhong(normal,
                            viewDirection,
                            safeNormalize(offset),
                            pointLights.values[i].radiance.rgb / distanceSquared,
                            albedo);
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
                            albedo);
    }

    outColor = vec4(color, 1.0);
}