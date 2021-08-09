#version 440 core

uniform vec3 lightPos;
uniform float lightOuterRadius;
uniform vec3 lightColor;
uniform float shininess;
uniform float globalSpecMultiplier;

uniform vec3 ambientColor;

uniform vec3 cameraPos;

uniform sampler2D diffuseTex;
uniform sampler2D normalTex;
uniform sampler2D specularColorTex;
uniform sampler2D specularPowerTex;

in vec4 v_worldPos;
in vec3 v_normal;
in vec2 v_texCoord;
out vec4 fragColor;

vec3 specular(vec3 lightToSurface, vec3 normal, vec3 specularColor, float shiny, float specularPower)
{
    vec3 reflection = reflect(lightToSurface, normal);
    vec3 viewDir = normalize(cameraPos - v_worldPos.xyz);
    float RdotV = dot(reflection, viewDir);
    return specularColor * pow(max(RdotV, 0), specularPower * shiny);
}

float attenuate(float value, float maximum)
{
    float clampedValue = min(value, maximum);
    return 1.0 / (pow(5 * clampedValue / maximum, 2) + 1);
}

void main()
{
    vec4 diffuse = texture2D(diffuseTex, v_texCoord);
    vec3 specularColor = texture2D(specularColorTex, v_texCoord).rgb;
    float specularPower = texture2D(specularPowerTex, v_texCoord).r;

    if (diffuse.a < 0.1f)
        discard;
    
    // Point lights are omni directional, so our light direction for this case would be the direction
    // from our gemoetry surface to the light position
    vec3 surfaceToLight = lightPos - v_worldPos.xyz;
    vec3 lightDir = -normalize(surfaceToLight);
    vec3 normal = normalize(v_normal);
    float NdotL = clamp(dot(normal, -lightDir), 0, 1);

    float distance = sqrt(dot(surfaceToLight, surfaceToLight));
    float gradient = attenuate(distance, lightOuterRadius);
    vec3 spec = gradient * globalSpecMultiplier * specular(lightDir, normal, specularColor, shininess, specularPower);
    
    vec3 lightContrib = lightColor * gradient * vec3(NdotL) + ambientColor + spec;
    fragColor = vec4(lightContrib * diffuse.xyz, diffuse.a);
}