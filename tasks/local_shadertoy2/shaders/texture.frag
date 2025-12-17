#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) out vec4 color;

layout (location = 0 ) in VS_OUT
{
  vec2 texCoord;
} surf;

layout(push_constant) uniform params
{
  float time;
  float xMouse;
} pushConstant;

void main() {
    vec2 uv = surf.texCoord;

    float t = pushConstant.time * 1.0;

    vec3 c1 = vec3(0.5 + 0.5*sin(t), 0.3 + 0.3*sin(t+2.0), 0.6 + 0.4*sin(t+4.0));
    vec3 c2 = vec3(0.2 + 0.5*sin(t+1.0), 0.5 + 0.4*sin(t+3.0), 0.3 + 0.5*sin(t+5.0));

    vec3 col = mix(c1, c2, uv.y);

    color = vec4(col, 1.0);
}