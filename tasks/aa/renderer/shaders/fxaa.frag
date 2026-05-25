#version 450

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D colorTex;

layout(push_constant) uniform params_t {
  vec2 rcpFrame;
  int  mode;       // 0 = No AA, 1 = FXAA Basic, 2 = FXAA 3.11
  float _pad;
} pc;


float luma(vec3 c) {
  return dot(c, vec3(0.299, 0.587, 0.114));
}


vec4 fxaaBasic(vec2 uv) {
  vec2 r = pc.rcpFrame;

  vec3 rgbNW = texture(colorTex, uv + vec2(-r.x,  r.y)).rgb;
  vec3 rgbNE = texture(colorTex, uv + vec2( r.x,  r.y)).rgb;
  vec3 rgbSW = texture(colorTex, uv + vec2(-r.x, -r.y)).rgb;
  vec3 rgbSE = texture(colorTex, uv + vec2( r.x, -r.y)).rgb;
  vec3 rgbM  = texture(colorTex, uv).rgb;

  float lumaNW = luma(rgbNW);
  float lumaNE = luma(rgbNE);
  float lumaSW = luma(rgbSW);
  float lumaSE = luma(rgbSE);
  float lumaM  = luma(rgbM);

  vec2 dir;
  dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
  dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));

  float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
  float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));
  float lumaRange = lumaMax - lumaMin;

  if (lumaRange < max(0.0625, lumaMax * 0.125))
    return vec4(rgbM, 1.0);

  float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * 0.25 * 0.125, 1.0 / 128.0);
  float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
  dir = clamp(dir * rcpDirMin, vec2(-8.0), vec2(8.0)) * r;

  vec3 rgbA = 0.5 * (
    texture(colorTex, uv + dir * (1.0/3.0 - 0.5)).rgb +
    texture(colorTex, uv + dir * (2.0/3.0 - 0.5)).rgb);

  vec3 rgbB = rgbA * 0.5 + 0.25 * (
    texture(colorTex, uv + dir * -0.5).rgb +
    texture(colorTex, uv + dir *  0.5).rgb);

  float lumaB = luma(rgbB);

  if (lumaB < lumaMin || lumaB > lumaMax)
    return vec4(rgbA, 1.0);

  return vec4(rgbB, 1.0);
}



#define FXAA_EDGE_THRESHOLD      (1.0/8.0)
#define FXAA_EDGE_THRESHOLD_MIN  (1.0/24.0)
#define FXAA_SUBPIX_TRIM         (1.0/4.0)
#define FXAA_SUBPIX_TRIM_SCALE   (3.0/4.0)
#define FXAA_SUBPIX_CAP          (7.0/8.0)
#define FXAA_SEARCH_STEPS        12
#define FXAA_SEARCH_ACCELERATION 1

float fxaaSearchStep(int i) {
  const float steps[12] = float[12](1,1,1,1,1,1,2,2,2,2,4,8);
  return steps[i];
}

vec4 fxaa311(vec2 uv) {
  vec2 r = pc.rcpFrame;

  float lumaN  = luma(texture(colorTex, uv + vec2( 0.0,  r.y)).rgb);
  float lumaS  = luma(texture(colorTex, uv + vec2( 0.0, -r.y)).rgb);
  float lumaE  = luma(texture(colorTex, uv + vec2( r.x,  0.0)).rgb);
  float lumaW  = luma(texture(colorTex, uv + vec2(-r.x,  0.0)).rgb);
  float lumaM  = luma(texture(colorTex, uv).rgb);

  float lumaMin = min(lumaM, min(min(lumaN, lumaS), min(lumaE, lumaW)));
  float lumaMax = max(lumaM, max(max(lumaN, lumaS), max(lumaE, lumaW)));
  float lumaRange = lumaMax - lumaMin;

  if (lumaRange < max(FXAA_EDGE_THRESHOLD_MIN, lumaMax * FXAA_EDGE_THRESHOLD))
    return texture(colorTex, uv);

  float lumaNW = luma(texture(colorTex, uv + vec2(-r.x,  r.y)).rgb);
  float lumaNE = luma(texture(colorTex, uv + vec2( r.x,  r.y)).rgb);
  float lumaSW = luma(texture(colorTex, uv + vec2(-r.x, -r.y)).rgb);
  float lumaSE = luma(texture(colorTex, uv + vec2( r.x, -r.y)).rgb);

  float lumaL = (lumaN + lumaS + lumaE + lumaW) * 0.25;
  float rangeL = abs(lumaL - lumaM);
  float blendL = max(0.0,
    (rangeL / lumaRange) - FXAA_SUBPIX_TRIM) * FXAA_SUBPIX_TRIM_SCALE;
  blendL = min(FXAA_SUBPIX_CAP, blendL);


  float edgeH = abs(lumaNW - lumaW) + abs(lumaN - lumaM) * 2.0 + abs(lumaNE - lumaE)
              + abs(lumaW  - lumaSW) + abs(lumaS - lumaM) * 2.0 + abs(lumaE  - lumaSE);
  float edgeV = abs(lumaNW - lumaN) + abs(lumaW - lumaM) * 2.0 + abs(lumaSW - lumaS)
              + abs(lumaN  - lumaNE) + abs(lumaE - lumaM) * 2.0 + abs(lumaS  - lumaSE);

  bool horzSpan = edgeH >= edgeV;


  vec2 perpStep = horzSpan ? vec2(0.0, r.y) : vec2(r.x, 0.0);

  vec2 alongStep = horzSpan ? vec2(r.x, 0.0) : vec2(0.0, r.y);


  float luma1 = horzSpan ? lumaN : lumaE;
  float luma2 = horzSpan ? lumaS : lumaW;
  float grad1 = abs(luma1 - lumaM);
  float grad2 = abs(luma2 - lumaM);


  bool side1 = grad1 >= grad2;
  vec2 uvEdge = uv + perpStep * (side1 ? 0.5 : -0.5);
  float gradScaled = 0.25 * max(grad1, grad2);

  float lumaEnd1, lumaEnd2;
  bool done1 = false, done2 = false;

  vec2 uv1 = uvEdge - alongStep;
  vec2 uv2 = uvEdge + alongStep;
  float lumaEdge = luma(texture(colorTex, uvEdge).rgb);

  for (int i = 0; i < FXAA_SEARCH_STEPS; ++i) {
    float s = fxaaSearchStep(i);

    if (!done1) {
      lumaEnd1 = luma(texture(colorTex, uv1).rgb) - lumaEdge;
      done1 = abs(lumaEnd1) >= gradScaled;
      uv1 -= alongStep * s;
    }
    if (!done2) {
      lumaEnd2 = luma(texture(colorTex, uv2).rgb) - lumaEdge;
      done2 = abs(lumaEnd2) >= gradScaled;
      uv2 += alongStep * s;
    }
    if (done1 && done2) break;
  }

  float dist1 = horzSpan ? (uv.x - uv1.x) : (uv.y - uv1.y);
  float dist2 = horzSpan ? (uv2.x - uv.x) : (uv2.y - uv.y);
  bool closerToEnd1 = dist1 < dist2;
  float distMin = min(dist1, dist2);
  float edgeSpan = dist1 + dist2;


  bool goodSpan = ((closerToEnd1 ? lumaEnd1 : lumaEnd2) < 0.0) !=
                  ((lumaM - lumaEdge) < 0.0);

  float spanBlend = goodSpan ? (0.5 - distMin / edgeSpan) : 0.0;
  float finalBlend = max(blendL, spanBlend);

  vec2 uvFinal = uv + perpStep * (side1 ? -finalBlend : finalBlend);
  return texture(colorTex, uvFinal);
}


void main() {
  vec2 uv = gl_FragCoord.xy * pc.rcpFrame;

  if (pc.mode == 0) {
    outColor = texture(colorTex, uv);
  } else if (pc.mode == 1) {
    outColor = fxaaBasic(uv);
  } else {
    outColor = fxaa311(uv);
  }
}