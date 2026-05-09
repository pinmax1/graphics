#version 450

layout(push_constant) uniform params_t
{
  mat4 mProjView;
  vec4 chunkOffset;
  vec4 camPos;
} params;

const vec2 positions[4] = vec2[4](
  vec2(0.0, 0.0),
  vec2(1.0, 0.0),
  vec2(1.0, 1.0),
  vec2(0.0, 1.0)
);

layout(location = 0) out vec2 vUV;

void main()
{
  vec2 pos = positions[gl_VertexIndex];
  vUV = pos;

  vec2 worldPos = params.chunkOffset.xy + pos * params.chunkOffset.zw;

  gl_Position = vec4(worldPos.x, 0.0, worldPos.y, 1.0);
}
