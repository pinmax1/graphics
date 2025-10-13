#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) out vec4 color;

layout (binding = 0) uniform sampler2D colorTex;

layout (binding = 1) uniform sampler2D otherTex;

layout (location = 0 ) in VS_OUT
{
  vec2 texCoord;
} surf;

layout(push_constant) uniform params
{
  float time;
  float xMouse;
} pushConstant;



int maxRayMarchIter = 250;
float maxRayMarchDist = 100.0f;

struct Surface {
    int channel_id;
    float sd;
};

// Rotation matrix around the X axis.
mat3 rotateX(float theta) {
    float c = cos(theta);
    float s = sin(theta);
    return mat3(
        vec3(1, 0, 0),
        vec3(0, c, -s),
        vec3(0, s, c)
    );
}

// Rotation matrix around the Y axis.
mat3 rotateY(float theta) {
    float c = cos(theta);
    float s = sin(theta);
    return mat3(
        vec3(c, 0, s),
        vec3(0, 1, 0),
        vec3(-s, 0, c)
    );
}

// Rotation matrix around the Z axis.
mat3 rotateZ(float theta) {
    float c = cos(theta);
    float s = sin(theta);
    return mat3(
        vec3(c, -s, 0),
        vec3(s, c, 0),
        vec3(0, 0, 1)
    );
}

// Identity matrix.
mat3 identity() {
    return mat3(
        vec3(1, 0, 0),
        vec3(0, 1, 0),
        vec3(0, 0, 1)
    );
}


Surface sdSphere(vec3 p, vec3 offset, float r, int id) {
    p = (p - offset);
    return Surface(id, length(p) - r);
}
Surface sdPlane(vec3 p, float offset, int id) {
    return Surface(id, (p.y - offset));
}

Surface sdTorus( vec3 p, vec2 t, vec3 offset, int id, mat3 transform)
{
  p -= offset;
  p *= transform;
  Surface s;
  s.channel_id = id;
  vec2 q = vec2(length(p.xz)-t.x,p.y);
  s.sd = length(q) - t.y;
  return s;
}

Surface sdLink( vec3 p, float le, float r1, float r2, vec3 offset, int id, mat3 transform)
{
  p -= offset;
  p *= transform;
  Surface s;
  s.channel_id = id;
  vec3 q = vec3( p.x, max(abs(p.y)-le,0.0), p.z );
  
  s.sd = length(vec2(length(q.xy)-r1,q.z)) - r2;
  return s;
}

Surface sdCylinder(vec3 p, float h, float r, vec3 offset, int id, mat3 transform)
{
    p -= offset;
    p *= transform;
    vec2 d = abs(vec2(length(p.xz),p.y)) - vec2(r,h);
    Surface surface;
    surface.sd = min(max(d.x,d.y),0.0) + length(max(d,0.0));
    surface.channel_id = id;
    
    return surface;
}

Surface sdVerticalCapsule( vec3 p, float h, float r, vec3 offset, int id, mat3 transform, float scalingParam)
{
  p -= offset;
  p *= transform;
  p.y -= clamp( p.y, 0.0, h );
  Surface s;
  s.channel_id = id;
  s.sd = length( p ) - r - scalingParam * (1.0 + sin(3.0 * pushConstant.time));
  return s;
}

Surface minWithSurface(Surface first, Surface second) {
    if(first.sd < second.sd) return first;
    return second;
}

Surface sdScene(vec3 p) {
    mat3 headRotation = rotateZ(pushConstant.time) * rotateX(pushConstant.time);
    Surface head = sdSphere(p, vec3(0.0, 6.0,0.0), 1.5, 3);
    Surface capsule = sdVerticalCapsule(p, 3.0, 0.9, vec3(0.0, 1.5,0.0), 2, identity(), 0.1);
    Surface rightHand = sdVerticalCapsule(p, 2.0, 0.5, vec3(1.0, 3.5,0.0), 2, rotateZ(2.9) * rotateX(0.5 * sin(3.0 * pushConstant.time)), 0.0);
    Surface leftHand = sdVerticalCapsule(p, 2.0, 0.5, vec3(-1.0, 3.5,0.0), 2, rotateZ(sin(5.0 * pushConstant.time) * 0.2 - 0.6), 0.0);
    vec3 floorColor = vec3(1. + 0.7*mod(floor(p.x) + floor(p.z + 3.0 * pushConstant.time), 2.0));
    Surface plane = sdPlane(p, -1.9, 1);
    Surface rightLeg = sdVerticalCapsule(p, 2.5, 0.4, vec3(0.5, 0.9,0.0), 2, rotateX(3.14 + sin(3.0 * pushConstant.time) * 0.5), 0.0);
    Surface leftLeg = sdVerticalCapsule(p, 2.5, 0.4, vec3(-0.5, 0.9,0.0), 2, rotateX(3.14 - sin(3.0 * pushConstant.time) * 0.5), 0.0);
    Surface res = capsule;
    res = minWithSurface(res, head);
    res = minWithSurface(res, plane);
    res = minWithSurface(res, rightHand);
    res = minWithSurface(res, leftHand);
    res = minWithSurface(res, leftLeg);
    res = minWithSurface(res, rightLeg);
    
    return res;
    
}

vec3 calcNormal(vec3 p) {
  vec2 e = vec2(1.0, -1.0) * 0.0005;
  float r = 1.;
  return normalize(
    e.xyy * sdScene(p + e.xyy).sd +
    e.yyx * sdScene(p + e.yyx).sd +
    e.yxy * sdScene(p + e.yxy).sd +
    e.xxx * sdScene(p + e.xxx).sd);
}

Surface trace(vec3 ro, vec3 rd, float epsilon) {
    float totalDist = 0.0;
    Surface closestObj;
    for(int i = 0; i < maxRayMarchIter; ++i) {
        vec3 p = ro + rd * totalDist;
        closestObj = sdScene(p);
        if(closestObj.sd < epsilon) break;
        totalDist += 0.7 * closestObj.sd;
        if(totalDist > maxRayMarchDist) break;
    }
    closestObj.sd = totalDist;
    return closestObj;
}
vec3 getTextureFromId(int id, vec3 p, vec3 w) {
    vec3 col;
    float tiling = 0.25;
    vec3 p_tiled = fract(p * tiling);
    if (id == 1) {
        col = w.x * textureLod(otherTex, p_tiled.yz, 0).rgb +
              w.y * textureLod(otherTex, fract(p_tiled.xz + vec2(0.0, pushConstant.time)), 0).rgb +
              w.z * textureLod(otherTex, p_tiled.xy, 0).rgb;
    } else if (id == 3) {
        col = w.x * textureLod(colorTex, p.yz, 0).rgb +
              w.y * textureLod(colorTex, p.xz, 0).rgb +
              w.z * textureLod(colorTex, p.xy, 0).rgb;
    } else {
        col = w.x * textureLod(otherTex, p_tiled.yz, 0).rgb +
              w.y * textureLod(otherTex, p_tiled.xz, 0).rgb +
              w.z * textureLod(otherTex, p_tiled.xy, 0).rgb;
    }
    
    return col;
}
vec3 light(vec3 lightDir, vec3 normal, vec3 camDir, vec3 col) {
    vec3 lightCol = vec3(1.0);

    float k_a = 0.25;
    vec3 i_a = col;
    vec3 ambient = k_a * i_a;

    float k_d = 0.6;
    float dotLN = clamp(dot(lightDir, normal), 0., 1.);
    vec3 i_d = col;
    vec3 diffuse = k_d * dotLN * i_d;

    float k_s = 0.7;
    float dotNV = clamp(dot(normalize(lightDir + camDir), normal), 0., 1.);
    vec3 i_s = lightCol;
    float alpha = 150.;
    vec3 specular = k_s * pow(dotNV, alpha) * i_s;

    return ambient + diffuse + specular;


}




void main()
{
    vec3 iMouse = vec3(pushConstant.xMouse, 0.0, 0.0);
    vec2 iResolution = vec2(1280, 720);
    vec3 mouse = vec3(iMouse.xy/iResolution.xy - 0.5,iMouse.z-.5);
    mouse *= 2.0 * 3.14;
    vec3 cameraPos = vec3(15.0 * cos(mouse.x), 1.0, 15.0 * sin(mouse.x));
    vec3 cameraForward = normalize(vec3(0,1.0,0) - cameraPos);
    vec3 cameraUp = vec3(0.0,1.0,0.0);
    vec3 cameraRight = normalize(cross(cameraUp,cameraForward));
    vec3 lightPos1 = vec3(0.0 ,4.0,4.0);
    vec3 lightPos2 = vec3(0.0 ,12.0,0.0);
    vec3 lightCol = vec3(1.0,1.0, 1.0);
    vec2 uv = (surf.texCoord - vec2(0.5));
    uv.x *= iResolution.x/iResolution.y;
    uv.y = -uv.y;
    vec3 ro = cameraPos;
    float fov = 1.0;
    vec3 rd = normalize(uv.x * cameraRight + uv.y * cameraUp + fov * cameraForward);
    Surface s = trace(ro,rd, 0.001);
    vec3 col = vec3(0.4, 0.6, 0.8);
    if(s.sd < maxRayMarchDist) {
        vec3 p = ro + rd * s.sd;
        vec3 norm = calcNormal(p);
        vec3 lightDir1 = normalize(lightPos1 - p);
        vec3 lightDir2 = normalize(lightPos2 - p);
        vec3 camDir = normalize(cameraPos - p);
        float lightIntensity1 = 0.7;
        float lightIntensity2 = 0.1;
        
        vec3 w = abs(norm);
        vec3 texCol = getTextureFromId(s.channel_id, p, w);
        col = lightIntensity1 * light(lightDir1, norm, camDir, texCol);
        col += lightIntensity2 * light(lightDir2, norm, camDir, texCol);
    }
    color = vec4(col, 1.0);
}