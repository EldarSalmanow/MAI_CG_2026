#ifndef CG_SCENE_INTERFACE_GLSL
#define CG_SCENE_INTERFACE_GLSL

layout(std140, set = 0, binding = 0) uniform SceneUniform {
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 ambient;
    vec4 lightDirection;
    vec4 directionalRadiance;
    uint pointLightCount;
    uint spotLightCount;
    uint lightingEnabled;
    uint reserved;
} scene;

layout(std140, set = 1, binding = 0) uniform ModelUniform {
    mat4 model;
    mat4 normalMatrix;
    // Each vec3/float pair occupies the same 16 bytes as its C++ vec4.
    vec3 albedo;
    float vertexColorWeight;
    vec3 specular;
    float shininess;
    vec3 emission;
    float unlit;
} object;

#endif
