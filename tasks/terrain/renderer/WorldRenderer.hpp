#pragma once

#include <etna/Image.hpp>
#include <etna/Sampler.hpp>
#include <etna/Buffer.hpp>
#include <etna/GraphicsPipeline.hpp>
#include <glm/glm.hpp>

#include "scene/SceneManager.hpp"
#include "wsi/Keyboard.hpp"

#include "FramePacket.hpp"


class WorldRenderer
{
public:
  WorldRenderer();

  void loadScene(std::filesystem::path path);

  void loadShaders();
  void allocateResources(glm::uvec2 swapchain_resolution);
  void setupPipelines(vk::Format swapchain_format);

  void debugInput(const Keyboard& kb);
  void update(const FramePacket& packet);
  void drawGui();
  void renderWorld(
    vk::CommandBuffer cmd_buf, vk::Image target_image, vk::ImageView target_image_view);

private:
  void renderScene(
    vk::CommandBuffer cmd_buf, const glm::mat4x4& glob_tm, vk::PipelineLayout pipeline_layout);

  /// Генерирует heightmap 4096x4096 (R32_SFLOAT) многооктавным шумом Перлина
  /// и загружает на GPU как etna::Image. Вызывается один раз при инициализации.
  void generateHeightmap();

  /// Рисует террейн как сетку чанков. Каждый чанк — один draw call с 4 вершинами
  /// (патч для тесселляции). Descriptor set с heightmap биндится до вызова.
  void renderTerrain(vk::CommandBuffer cmd_buf, vk::DescriptorSet descriptorSet);

private:
  std::unique_ptr<SceneManager> sceneMgr;

  etna::Image mainViewDepth;
  etna::Buffer constants;

  /// Карта высот террейна. Одноканальная R32_SFLOAT текстура 4096x4096.
  /// Значения [0, 1], генерируется шумом Перлина.
  /// Семплируется в TESE для смещения вершин по Y.
  etna::Image heightmap;

  /// Сэмплер для heightmap: билинейная фильтрация, clamp to edge.
  etna::Sampler heightmapSampler;

  // Push constants для отрисовки моделей (не используется для террейна)
  struct PushConstants
  {
    glm::mat4x4 projView;
    glm::mat4x4 model;
  } pushConst2M;

  /// Push constants для террейна. Передаются во все 4 стадии шейдеров:
  /// vert (позиции углов чанка), tesc (LOD по расстоянию до камеры),
  /// tese (MVP проекция), frag (позиция камеры для эффектов).
  struct TerrainPushConstants
  {
    glm::mat4x4 projView;     // Матрица View-Projection
    glm::vec4 chunkOffset;    // xy = мировое смещение чанка, zw = размер чанка
    glm::vec4 camPos;         // xyz = позиция камеры (для вычисления LOD в TCS)
  };

  glm::mat4x4 worldViewProj;  // Текущая View-Projection матрица
  glm::vec3 cameraPosition;   // Позиция камеры (для передачи в шейдеры)
  glm::mat4x4 lightMatrix;

  etna::GraphicsPipeline staticMeshPipeline{};

  /// Пайплайн террейна: topology = PatchList, 4 control points,
  /// использует все 4 программируемые стадии (vert, tesc, tese, frag).
  etna::GraphicsPipeline terrainPipeline{};

  glm::uvec2 resolution;
};
