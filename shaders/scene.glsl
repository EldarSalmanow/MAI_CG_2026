#ifndef CG_SCENE_GLSL
#define CG_SCENE_GLSL

layout(std140, set = 0, binding = 0) uniform SceneUniform {
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 ambientRadiance;
    vec4 directionalLightDirection;
    vec4 directionalLightRadiance;
    uint pointLightCount;
    uint spotLightCount;
    uint lightingEnabled;
    uint reserved;
    mat4 directionalLightViewProjection;
    mat4 spotLightViewProjection;
    uint directionalShadowEnabled;
    uint spotShadowEnabled;
    uint shadowSpotLightIndex;
    uint shadowReserved;
} scene;

layout(std140, set = 1, binding = 0) uniform ModelUniform {
    mat4 model;
    mat4 normalMatrix;
    vec3 albedo;
    float vertexColorWeight;
    vec3 specular;
    float shininess;
    vec3 unlitColor;
    uint unlit;
} object;

struct PointLightData {
    vec4 position;
    vec4 radiance;
};

struct SpotLightData {
    vec4 position;
    vec4 radiance;
    vec4 direction;
    float innerConeCosine;
    float outerConeCosine;
    vec2 reserved;
};

layout(std430, set = 0, binding = 1) readonly buffer PointLights {
    PointLightData values[];
} pointLights;

layout(std430, set = 0, binding = 2) readonly buffer SpotLights {
    SpotLightData values[];
} spotLights;

#endif
