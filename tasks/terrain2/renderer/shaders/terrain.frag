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
  vec3 lightDir = normalize(vec3(1.0, 1.0, 0.5));
  vec3 normal = normalize(teNormal);

  vec2 globalUV = teWorldPos.xz / terrainSize;
  
  vec2 detailUV = teWorldPos.xz / terrainSize * tiling;

  vec4 splat = texture(splatMap, globalUV);
  float wGravel = splat.r;
  float wSand   = splat.g;
  float wGround = splat.b;
  float wSnow   = splat.a;

  vec3 colGravel = texture(gravelTex, detailUV).rgb;
  vec3 colSand   = texture(sandTex, detailUV).rgb;
  vec3 colGround = texture(groundTex, detailUV).rgb;
  vec3 colSnow   = texture(snowTex, detailUV).rgb;

  float hGravel = texture(gravelHeight, detailUV).r;
  float hSand   = texture(sandHeight, detailUV).r;
  float hGround = texture(groundHeight, detailUV).r;
  float hSnow   = texture(snowHeight, detailUV).r;

  float sharpness = 8.0;
  wGround = exp((wGround + hGround) * sharpness);
  wSand = exp((wSand + hSand)   * sharpness);
  wSnow = exp((wSnow + hSnow)   * sharpness);
  wGravel = exp((wGravel + hGravel) * sharpness);

  float wTotal = wGround + wSand + wSnow + wGravel;
  wGround /= wTotal;
  wSand /= wTotal;
  wSnow /= wTotal;
  wGravel /= wTotal;

  vec3 surfaceColor = colGround * wGround
                    + colSand   * wSand
                    + colSnow   * wSnow
                    + colGravel * wGravel;

  float ambient = 0.15;
  float diffuse = max(dot(normal, lightDir), 0.0);

  outColor = vec4((ambient + diffuse) * surfaceColor, 1.0);
}