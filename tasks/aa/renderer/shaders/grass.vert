#version 450


layout(push_constant) uniform params_t
{
  mat4 mProjView;
  vec4 camPos;
  float time;
} params;

struct GrassBlade
{
  vec4 posAndRotation;
  vec4 bladeParams;    // x = высота y = ширина z = windPhase
};

layout(set = 0, binding = 0) readonly buffer GrassInstances
{
  uint count;
  uint pad0, pad1, pad2;
  GrassBlade blades[];
} grassData;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out float fragAlpha;

vec2 windOffset(vec2 worldXZ, float t, float phase)
{
  float wx = sin(worldXZ.x * 0.01 + t * 0.8 + phase * 6.28) * 0.7

           + sin(worldXZ.y * 0.02 + t * 0.5 + phase * 2.0) * 0.3;

  float wz = sin(worldXZ.y * 0.012 + t * 0.7 + phase * 4.0) * 0.7
           + sin(worldXZ.x * 0.025 + t * 0.4) * 0.3;

  return vec2(wx, wz) * 0.08;
}

void main()
{
  GrassBlade blade = grassData.blades[gl_InstanceIndex];

  vec3 basePos = blade.posAndRotation.xyz;
  float rotation = blade.posAndRotation.w;
  float bladeHeight = blade.bladeParams.x;
  float bladeWidth = blade.bladeParams.y;
  float windPhase = blade.bladeParams.z;

  int segIdx = gl_VertexIndex / 2;
  int side = gl_VertexIndex % 2;
  float t = float(segIdx) / 3.0;

  if (gl_VertexIndex == 6)
  {
    t = 1.0;
    side = 0;
  }

  float width = bladeWidth * (1.0 - t * 0.9);
  float localX = (side == 0 ? -width : width) * 0.5;
  if (gl_VertexIndex == 6)
    localX = 0.0;
  float localY = t * bladeHeight;

  vec2 wind = windOffset(basePos.xz, params.time, windPhase) * t * t;

  float c = cos(rotation);
  float s = sin(rotation);
  vec3 worldOffset = vec3(
    localX * c + wind.x,
    localY,
    localX * s + wind.y
  );

  vec3 worldPos = basePos + worldOffset;

  gl_Position = params.mProjView * vec4(worldPos, 1.0);

  vec3 baseColor = vec3(0.1, 0.3, 0.05);
  vec3 tipColor = vec3(0.3, 0.6, 0.1);
  fragColor = mix(baseColor, tipColor, t);
  fragAlpha = 1.0;
}
