#version 450

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform params_t
{
  float worldSize;
  int cascadeIdx;
  vec4 clipmapCenter;
} params;

layout(set = 0, binding = 0) uniform sampler2D heightmapTex;
layout(set = 0, binding = 1) uniform sampler2D splatMap;
layout(set = 0, binding = 2) uniform sampler2D gravelTex;
layout(set = 0, binding = 3) uniform sampler2D sandTex;
layout(set = 0, binding = 4) uniform sampler2D groundTex;
layout(set = 0, binding = 5) uniform sampler2D snowTex;
layout(set = 0, binding = 6) uniform sampler2D gravelHeight;
layout(set = 0, binding = 7) uniform sampler2D sandHeight;
layout(set = 0, binding = 8) uniform sampler2D groundHeight;
layout(set = 0, binding = 9) uniform sampler2D snowHeight;

const float terrainSize = 100.0;
const float tiling = 20.0;

void main()
{
  vec2 uv = gl_FragCoord.xy / 1024.0;
  vec2 worldXZ = params.clipmapCenter.xz + (uv - 0.5) * params.worldSize;

  if (worldXZ.x < 0.0 || worldXZ.x > terrainSize ||
      worldXZ.y < 0.0 || worldXZ.y > terrainSize)
  {
    outColor = vec4(0.0);
    return;
  }

  vec2 globalUV = worldXZ / terrainSize;
  vec2 detailUV = worldXZ / terrainSize * tiling;

  vec4 splat = texture(splatMap, globalUV);
  float wGravel = splat.r;
  float wSand   = splat.g;
  float wGround = splat.b;
  float wSnow   = splat.a;

  vec3 colGravel = texture(gravelTex, detailUV).rgb;
  vec3 colSand   = texture(sandTex,   detailUV).rgb;
  vec3 colGround = texture(groundTex, detailUV).rgb;
  vec3 colSnow   = texture(snowTex,   detailUV).rgb;

  float hGravel = texture(gravelHeight, detailUV).r;
  float hSand   = texture(sandHeight,   detailUV).r;
  float hGround = texture(groundHeight, detailUV).r;
  float hSnow   = texture(snowHeight,   detailUV).r;

  float sharpness = 8.0;
  wGravel = exp((wGravel + hGravel) * sharpness);
  wSand   = exp((wSand   + hSand)   * sharpness);
  wGround = exp((wGround + hGround) * sharpness);
  wSnow   = exp((wSnow   + hSnow)   * sharpness);

  float wTotal = wGravel + wSand + wGround + wSnow;
  wGravel /= wTotal;
  wSand   /= wTotal;
  wGround /= wTotal;
  wSnow   /= wTotal;

  vec3 color = colGravel * wGravel
             + colSand   * wSand
             + colGround * wGround
             + colSnow   * wSnow;

  outColor = vec4(color, 1.0);
}