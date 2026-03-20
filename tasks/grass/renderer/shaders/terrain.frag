#version 450

layout(location = 0) in vec3 teWorldPos;
layout(location = 1) in vec3 teNormal;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform params_t
{
  mat4 mProjView;
  vec4 chunkOffset;
  vec4 camPos;
} params;

void main()
{
  vec3 lightDir = normalize(vec3(1.0, 1.0, 0.5));
  vec3 normal = normalize(teNormal);

  float h = teWorldPos.y / 15.0;
  vec3 lowColor = vec3(0.2, 0.5, 0.1);
  vec3 highColor = vec3(0.6, 0.5, 0.4);
  vec3 surfaceColor = mix(lowColor, highColor, clamp(h, 0.0, 1.0));

  float ambient = 0.15;
  float diffuse = max(dot(normal, lightDir), 0.0);

  outColor = vec4((ambient + diffuse) * surfaceColor, 1.0);
}
