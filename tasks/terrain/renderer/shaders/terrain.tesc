#version 450

// Tessellation Control Shader (TCS) террейна.
// Определяет уровень тесселляции для каждого ребра патча (чанка).
// Уровень тесселляции обратно пропорционален расстоянию от середины ребра до камеры:
// чем ближе камера — тем больше треугольников генерируется.
//
// Важно для шага 3 (борьба с дырками): outer tessellation levels вычисляются
// из мировых координат рёбер. Соседние чанки делят общее ребро с одинаковыми
// мировыми координатами, поэтому получают одинаковый tess level — вершины
// на границе совпадают и дырок не возникает.

layout(vertices = 4) out; // 4 контрольные точки на патч (квад)

layout(push_constant) uniform params_t
{
  mat4 mProjView;
  vec4 chunkOffset;
  vec4 camPos;
} params;

layout(location = 0) in vec2 vUV[];  // UV из вершинного шейдера
layout(location = 0) out vec2 tcUV[]; // Прокидываем UV дальше в TESE

// Вычисляет уровень тесселляции для ребра между точками p0 и p1.
// Чем ближе середина ребра к камере, тем выше уровень (больше треугольников).
float getTessLevel(vec3 p0, vec3 p1)
{
  vec3 mid = (p0 + p1) * 0.5;
  float dist = distance(mid, params.camPos.xyz);
  // 800 / dist дает высокую тесселляцию вблизи и низкую вдали.
  // Clamp [1, 64] — минимум 1 (без разбиения), максимум 64 (лимит Vulkan).
  float level = clamp(800.0 / dist, 1.0, 64.0);
  return level;
}

void main()
{
  // Прокидываем данные из вершинного шейдера в TESE без изменений
  tcUV[gl_InvocationID] = vUV[gl_InvocationID];
  gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;

  // Только первая инвокация задаёт tess levels (они общие для всего патча)
  if (gl_InvocationID == 0)
  {
    // Мировые позиции 4 углов патча (Y=0, высота ещё не применена)
    vec3 p0 = gl_in[0].gl_Position.xyz; // bottom-left
    vec3 p1 = gl_in[1].gl_Position.xyz; // bottom-right
    vec3 p2 = gl_in[2].gl_Position.xyz; // top-right
    vec3 p3 = gl_in[3].gl_Position.xyz; // top-left

    // Outer levels управляют разбиением внешних рёбер квада.
    // Vulkan spec для quads:
    //   outer[0] = левое ребро   (u=0, v: 0->1), вершины p0-p3
    //   outer[1] = нижнее ребро  (v=0, u: 0->1), вершины p0-p1
    //   outer[2] = правое ребро  (u=1, v: 0->1), вершины p1-p2
    //   outer[3] = верхнее ребро (v=1, u: 0->1), вершины p3-p2
    gl_TessLevelOuter[0] = getTessLevel(p0, p3);
    gl_TessLevelOuter[1] = getTessLevel(p0, p1);
    gl_TessLevelOuter[2] = getTessLevel(p1, p2);
    gl_TessLevelOuter[3] = getTessLevel(p2, p3);

    // Inner levels управляют разбиением внутренней области.
    // Берём max от противоположных outer levels для плавного перехода.
    //   inner[0] = горизонтальное разбиение (связано с нижним/верхним рёбрами)
    //   inner[1] = вертикальное разбиение (связано с левым/правым рёбрами)
    gl_TessLevelInner[0] = max(gl_TessLevelOuter[1], gl_TessLevelOuter[3]);
    gl_TessLevelInner[1] = max(gl_TessLevelOuter[0], gl_TessLevelOuter[2]);
  }
}
