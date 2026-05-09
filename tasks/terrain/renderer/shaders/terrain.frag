#version 450

// Фрагментный шейдер террейна.
// Вычисляет диффузное освещение (Ламберт) с направленным источником света.
// Цвет поверхности зависит от высоты: зелёный внизу, коричневый наверху.

layout(location = 0) in vec3 teWorldPos; // Мировая позиция из TESE
layout(location = 1) in vec3 teNormal;   // Нормаль поверхности из TESE

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform params_t
{
  mat4 mProjView;
  vec4 chunkOffset;
  vec4 camPos;
} params;

void main()
{
  // Направление к источнику света (направленный свет, как солнце)
  vec3 lightDir = normalize(vec3(1.0, 1.0, 0.5));
  vec3 normal = normalize(teNormal);

  // Цвет поверхности: линейная интерполяция по нормализованной высоте [0, 1].
  // heightScale = 15.0 в TESE, поэтому делим на 15.
  float h = teWorldPos.y / 15.0;
  vec3 lowColor = vec3(0.2, 0.5, 0.1);   // зелёный — низины, трава
  vec3 highColor = vec3(0.6, 0.5, 0.4);  // коричневый — возвышенности, камень
  vec3 surfaceColor = mix(lowColor, highColor, clamp(h, 0.0, 1.0));

  // Освещение: ambient + diffuse (Ламберт)
  float ambient = 0.15;
  float diffuse = max(dot(normal, lightDir), 0.0);

  outColor = vec4((ambient + diffuse) * surfaceColor, 1.0);
}
