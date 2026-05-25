#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in float fragAlpha;

layout(location = 0) out vec4 outColor;

void main()
{
  vec3 lightDir = normalize(vec3(1.0, 1.0, 0.5));

  vec3 normal = normalize(vec3(0.0, 1.0, 0.0));

  float ambient = 0.3;
  float diffuse = max(dot(normal, lightDir), 0.0) * 0.7;

  outColor = vec4(fragColor * (ambient + diffuse), fragAlpha);
}
