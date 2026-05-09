#version 450

// Вершинный шейдер террейна.
// Генерирует 4 вершины квада (патча) без использования вершинного буфера.
// Каждый патч — это один чанк террейна. Позиции углов вычисляются из
// push-константы chunkOffset (мировое смещение и размер чанка).
// gl_Position здесь содержит мировые координаты (X, 0, Z) — высота Y
// будет добавлена позже в tessellation evaluation шейдере при семплировании heightmap.

layout(push_constant) uniform params_t
{
  mat4 mProjView;
  vec4 chunkOffset; // xy = мировое смещение чанка, zw = размер чанка по X и Z
  vec4 camPos;      // xyz = позиция камеры (используется в TCS для LOD)
} params;

// Локальные координаты 4 углов квада [0,1]x[0,1].
// Порядок: bottom-left, bottom-right, top-right, top-left.
const vec2 positions[4] = vec2[4](
  vec2(0.0, 0.0),
  vec2(1.0, 0.0),
  vec2(1.0, 1.0),
  vec2(0.0, 1.0)
);

layout(location = 0) out vec2 vUV; // Локальные UV координаты внутри чанка [0,1]

void main()
{
  vec2 pos = positions[gl_VertexIndex];
  vUV = pos;

  // Переводим локальные координаты в мировые: offset + pos * size
  vec2 worldXZ = params.chunkOffset.xy + pos * params.chunkOffset.zw;

  // Y = 0 — высота будет задана в TESE из heightmap
  gl_Position = vec4(worldXZ.x, 0.0, worldXZ.y, 1.0);
}
