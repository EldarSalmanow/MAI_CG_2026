#ifndef CG_SCENE_INTERFACE_GLSL
#define CG_SCENE_INTERFACE_GLSL

layout(std140, set = 0, binding = 0) uniform SceneUniform {
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 ambient;
    vec4 lightDirection;
    vec4 directionalRadiance;
    uvec4 lightCounts;
} scene;

layout(std140, set = 1, binding = 0) uniform ModelUniform {
    mat4 model;
    mat4 normalMatrix;
    vec4 albedo;
    vec4 specular;
    vec4 emission;
} object;

#endif
