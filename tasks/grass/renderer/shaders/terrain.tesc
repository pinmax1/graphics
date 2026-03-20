#version 450

layout(vertices = 4) out;

layout(push_constant) uniform params_t
{
  mat4 mProjView;
  vec4 chunkOffset;
  vec4 camPos;
} params;

layout(location = 0) in vec2 vUV[];
layout(location = 0) out vec2 tcUV[];

float getTessLevel(vec3 p0, vec3 p1)
{
  vec3 mid = (p0 + p1) * 0.5;
  float dist = distance(mid, params.camPos.xyz);
  float level = clamp(800.0 / dist, 1.0, 64.0);
  return level;
}

void main()
{
  tcUV[gl_InvocationID] = vUV[gl_InvocationID];
  gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;

  if (gl_InvocationID == 0)
  {
    vec3 p0 = gl_in[0].gl_Position.xyz;
    vec3 p1 = gl_in[1].gl_Position.xyz;
    vec3 p2 = gl_in[2].gl_Position.xyz;
    vec3 p3 = gl_in[3].gl_Position.xyz;

    gl_TessLevelOuter[0] = getTessLevel(p0, p3);
    gl_TessLevelOuter[1] = getTessLevel(p0, p1);
    gl_TessLevelOuter[2] = getTessLevel(p1, p2);
    gl_TessLevelOuter[3] = getTessLevel(p2, p3);

    gl_TessLevelInner[0] = max(gl_TessLevelOuter[1], gl_TessLevelOuter[3]);
    gl_TessLevelInner[1] = max(gl_TessLevelOuter[0], gl_TessLevelOuter[2]);
  }
}
