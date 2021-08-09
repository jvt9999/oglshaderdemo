#version 440 core
uniform mat4 worldViewProjection;
uniform mat4 world;
layout (location = 0) in vec4 position;
layout (location = 1) in vec3 normal;
layout (location = 2) in vec2 texCoord;

out vec4 v_worldPos;
out vec3 v_normal;
out vec2 v_texCoord;

void main()
{
    gl_Position = worldViewProjection * position;
    v_worldPos = world * position;
    v_normal = mat3(world) * normal;
    v_texCoord = texCoord;
}