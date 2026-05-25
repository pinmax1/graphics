#version 450

// Tessellation Evaluation Shader (TESE) террейна.
// Вызывается для каждой сгенерированной вершины после тесселляции.
// Интерполирует мировую позицию из углов патча по gl_TessCoord,
// семплирует карту высот для смещения по Y, и вычисляет нормаль
// через конечные разности (finite differences) для освещения.
//
// quads — тесселляция квадов (не треугольников).
// fractional_odd_spacing — плавная интерполяция tess levels без резких скачков.
// cw — clockwise winding для совместимости с левосторонней проекцией (perspectiveLH).

layout(quads, fractional_odd_spacing, cw) in;

layout(push_constant) uniform params_t
{
  mat4 mProjView;
  vec4 chunkOffset;
  vec4 camPos;
} params;

// Карта высот 4096x4096, R32_SFLOAT, значения [0, 1].
// Генерируется на CPU многооктавным шумом Перлина.
layout(set = 0, binding = 0) uniform sampler2D heightmapTex;

// Размер террейна в мировых единицах (должен совпадать с terrainWorldSize в C++)
const float terrainWorldSize = 100.0;
// Максимальная высота террейна в мировых единицах
const float heightScale = 15.0;

layout(location = 0) in vec2 tcUV[];

layout(location = 0) out vec3 teWorldPos; // Мировая позиция для фрагментного шейдера
layout(location = 1) out vec3 teNormal;   // Нормаль поверхности для освещения

void main()
{
  // Билинейная интерполяция мировых позиций 4 углов патча по gl_TessCoord.xy.
  // gl_TessCoord.x = u (горизонталь), gl_TessCoord.y = v (вертикаль).
  // Сначала интерполируем по u (между left и right), потом по v (между bottom и top).
  vec3 p0 = mix(gl_in[0].gl_Position.xyz, gl_in[1].gl_Position.xyz, gl_TessCoord.x);
  vec3 p1 = mix(gl_in[3].gl_Position.xyz, gl_in[2].gl_Position.xyz, gl_TessCoord.x);
  vec3 worldPos = mix(p0, p1, gl_TessCoord.y);

  // UV для семплирования heightmap: мировые координаты XZ нормализуем в [0,1]
  vec2 uv = worldPos.xz / terrainWorldSize;

  // Семплируем высоту и масштабируем
  float h = texture(heightmapTex, uv).r * heightScale;
  worldPos.y = h;

  teWorldPos = worldPos;

  // Вычисление нормали через конечные разности (finite differences).
  // Семплируем 4 соседних текселя heightmap и по разности высот
  // строим касательные векторы, а их векторное произведение даёт нормаль.
  vec2 texelSize = 1.0 / vec2(textureSize(heightmapTex, 0));
  float hL = texture(heightmapTex, uv - vec2(texelSize.x, 0.0)).r * heightScale; // left
  float hR = texture(heightmapTex, uv + vec2(texelSize.x, 0.0)).r * heightScale; // right
  float hD = texture(heightmapTex, uv - vec2(0.0, texelSize.y)).r * heightScale; // down
  float hU = texture(heightmapTex, uv + vec2(0.0, texelSize.y)).r * heightScale; // up

  // Размер одного текселя в мировых единицах
  float worldTexelSize = terrainWorldSize * texelSize.x;

  // Касательный вектор по X: шаг 2*texel по X, разность высот hR-hL
  vec3 tangentX = vec3(2.0 * worldTexelSize, hR - hL, 0.0);
  // Касательный вектор по Z: шаг 2*texel по Z, разность высот hU-hD
  vec3 tangentZ = vec3(0.0, hU - hD, 2.0 * worldTexelSize);
  // Нормаль = cross(tangentZ, tangentX), направлена вверх (Y+)
  teNormal = normalize(cross(tangentZ, tangentX));

  // Проецируем в clip space
  gl_Position = params.mProjView * vec4(worldPos, 1.0);
}
