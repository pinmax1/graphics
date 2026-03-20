#version 450

layout(quads, fractional_odd_spacing, cw) in;

layout(push_constant) uniform params_t
{
  mat4 mProjView;
  vec4 chunkOffset;
  vec4 camPos;
} params;

layout(set = 0, binding = 0) uniform sampler2D heightmapTex;

const float terrainWorldSize = 100.0;

const float heightScale = 15.0;

layout(location = 0) in vec2 tcUV[];

layout(location = 0) out vec3 teWorldPos;
layout(location = 1) out vec3 teNormal;

void main()
{

  vec3 p0 = mix(gl_in[0].gl_Position.xyz, gl_in[1].gl_Position.xyz, gl_TessCoord.x);
  vec3 p1 = mix(gl_in[3].gl_Position.xyz, gl_in[2].gl_Position.xyz, gl_TessCoord.x);
  vec3 worldPos = mix(p0, p1, gl_TessCoord.y);

  vec2 uv = worldPos.xz / terrainWorldSize;

  float h = texture(heightmapTex, uv).r * heightScale;
  worldPos.y = h;

  teWorldPos = worldPos;

  vec2 texelSize = 1.0 / vec2(textureSize(heightmapTex, 0));
  float hL = texture(heightmapTex, uv - vec2(texelSize.x, 0.0)).r * heightScale;
  float hR = texture(heightmapTex, uv + vec2(texelSize.x, 0.0)).r * heightScale;
  float hD = texture(heightmapTex, uv - vec2(0.0, texelSize.y)).r * heightScale;
  float hU = texture(heightmapTex, uv + vec2(0.0, texelSize.y)).r * heightScale;

  float worldTexelSize = terrainWorldSize * texelSize.x;

  vec3 tangentX = vec3(2.0 * worldTexelSize, hR - hL, 0.0);

  vec3 tangentZ = vec3(0.0, hU - hD, 2.0 * worldTexelSize);

  teNormal = normalize(cross(tangentZ, tangentX));


  gl_Position = params.mProjView * vec4(worldPos, 1.0);
}
