#version 440 core
uniform mat4 worldViewProjection;
uniform mat4 world;
layout (location = 0) in vec4 position;
layout (location = 1) in vec3 normal;
layout (location = 2) in vec3 tangent;
layout (location = 3) in vec2 texCoord;

out vec4 v_worldPos;
out vec3 v_normal;
out vec3 v_tangent;
out vec3 v_binormal;
out vec2 v_texCoord;

void main()
{
    gl_Position = worldViewProjection * position;
    v_worldPos = world * position;
    v_normal = mat3(world) * normal;
    v_tangent = mat3(world) * tangent;
    v_binormal = cross(v_tangent, v_normal);
    v_texCoord = texCoord;
}