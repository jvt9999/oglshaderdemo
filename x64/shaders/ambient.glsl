#version 440 core

uniform vec3 ambientColor;

uniform sampler2D diffuseTex;
uniform sampler2D normalTex;
uniform sampler2D specularColorTex;
uniform sampler2D specularPowerTex;

in vec4 v_worldPos;
in vec3 v_normal;
in vec2 v_texCoord;
out vec4 fragColor;

void main()
{
    vec4 diffuse = texture2D(diffuseTex, v_texCoord);
    if (diffuse.a < 0.1f)
        discard;
    fragColor = vec4(ambientColor * diffuse.xyz, diffuse.a);
}