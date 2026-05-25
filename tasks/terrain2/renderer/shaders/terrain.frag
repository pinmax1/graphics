#version 450

layout(location = 0) in vec3 teWorldPos;
layout(location = 1) in vec3 teNormal;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform params_t
{
  mat4 mProjView;
  vec4 chunkOffset;
  vec4 camPos;
  vec4 clipmapCenter;
} params;

layout(set = 0, binding = 1) uniform sampler2D clipmap0;
layout(set = 0, binding = 2) uniform sampler2D clipmap1;
layout(set = 0, binding = 3) uniform sampler2D clipmap2;
layout(set = 0, binding = 4) uniform sampler2D clipmap3;

const float cascadeWorldSizes[4] = float[4](50.0, 100.0, 200.0, 400.0);

void main()
{
  vec3 lightDir = normalize(vec3(1.0, 1.0, 0.5));
  vec3 normal = normalize(teNormal);

  vec2 worldXZ = teWorldPos.xz;
  vec2 centerXZ = params.clipmapCenter.xz;

  float dist = max(abs(worldXZ.x - centerXZ.x), abs(worldXZ.y - centerXZ.y));

  vec3 surfaceColor;

  if (dist < cascadeWorldSizes[0] * 0.5)
  {
    vec2 uv = (worldXZ - centerXZ) / cascadeWorldSizes[0] + 0.5;
    surfaceColor = texture(clipmap0, uv).rgb;
  }
  else if (dist < cascadeWorldSizes[1] * 0.5)
  {
    vec2 uv = (worldXZ - centerXZ) / cascadeWorldSizes[1] + 0.5;
    surfaceColor = texture(clipmap1, uv).rgb;
  }
  else if (dist < cascadeWorldSizes[2] * 0.5)
  {
    vec2 uv = (worldXZ - centerXZ) / cascadeWorldSizes[2] + 0.5;
    surfaceColor = texture(clipmap2, uv).rgb;
  }
  else
  {
    vec2 uv = (worldXZ - centerXZ) / cascadeWorldSizes[3] + 0.5;
    surfaceColor = texture(clipmap3, uv).rgb;
  }

  float ambient = 0.15;
  float diffuse = max(dot(normal, lightDir), 0.0);

  outColor = vec4((ambient + diffuse) * surfaceColor, 1.0);
}