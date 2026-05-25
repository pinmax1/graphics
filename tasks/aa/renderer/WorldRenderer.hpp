#pragma once

#include <etna/Image.hpp>
#include <etna/Sampler.hpp>
#include <etna/Buffer.hpp>
#include <etna/GraphicsPipeline.hpp>
#include <etna/ComputePipeline.hpp>
#include <glm/glm.hpp>
#include <stb_image.h>

#include "scene/SceneManager.hpp"
#include "wsi/Keyboard.hpp"

#include "FramePacket.hpp"


class WorldRenderer
{
public:
  WorldRenderer();

  void loadScene(std::filesystem::path path);

  void loadShaders();
  void allocateResources(glm::uvec2 swapchain_resolution, vk::Format swapchain_format);
  void setupPipelines(vk::Format swapchain_format);

  void debugInput(const Keyboard& kb);
  void update(const FramePacket& packet);
  void drawGui();
  void renderWorld(
    vk::CommandBuffer cmd_buf, vk::Image target_image, vk::ImageView target_image_view);

private:
  void renderScene(
    vk::CommandBuffer cmd_buf, const glm::mat4x4& glob_tm, vk::PipelineLayout pipeline_layout);

  void generateHeightmap();
  void generateSplatmap();
  void loadDetailTextures();
  void allocateClipmapResources();
  void updateClipmapCascades(vk::CommandBuffer cmd_buf);
  void renderTerrain(vk::CommandBuffer cmd_buf, vk::DescriptorSet descriptorSet);
  void dispatchGrassPlacement(vk::CommandBuffer cmd_buf);
  void renderGrass(vk::CommandBuffer cmd_buf);
  void renderFxaa(vk::CommandBuffer cmd_buf, vk::Image target_image, vk::ImageView target_image_view);

private:
  std::unique_ptr<SceneManager> sceneMgr;

  etna::Image mainViewDepth;
  etna::Buffer constants;
  etna::Image colorBuffer;
  etna::Sampler colorSampler;

  std::vector<float> heightmapPixels;

  etna::Image heightmap;
  etna::Sampler heightmapSampler;

  etna::Image splatmap;
  etna::Sampler splatmapSampler;

  etna::Image detailTextures[4];
  etna::Image detailHeightmaps[4];
  etna::Sampler detailSampler;

  static constexpr int CLIPMAP_CASCADES = 4;
  static constexpr uint32_t CLIPMAP_SIZE = 1024;
  static constexpr float CLIPMAP_UPDATE_THRESHOLD = 2.0f;

  etna::Image clipmapImages[CLIPMAP_CASCADES];
  etna::Sampler clipmapSampler;
  glm::vec2 lastUpdatePos = glm::vec2(1e9f);
  glm::vec2 clipmapCenterPos = glm::vec2(0.0f);

  const float cascadeWorldSizes[CLIPMAP_CASCADES] = {50.0f, 100.0f, 200.0f, 400.0f};

  struct PushConstants
  {
    glm::mat4x4 projView;
    glm::mat4x4 model;
  } pushConst2M;

  struct TerrainPushConstants
  {
    glm::mat4x4 projView;
    glm::vec4 chunkOffset;
    glm::vec4 camPos;
    glm::vec4 clipmapCenter;
  };

  struct GrassPlacementPushConstants
  {
    glm::vec4 camPos;
    float time;
    float radius;
    float spacing;
    float terrainSize;
  };

  struct GrassRenderPushConstants
  {
    glm::mat4x4 projView;
    glm::vec4 camPos;
    float time;
  };

  struct ClipmapPushConstants
  {
    float worldSize;
    int cascadeIdx;
    float pad0, pad1;
    glm::vec4 clipmapCenter;
  };

  struct FxaaPushConstants {
    glm::vec2 rcpFrame;
    int mode;
    float pad;
  };

  glm::mat4x4 worldViewProj;
  glm::vec3 cameraPosition;
  float currentTime = 0.0f;
  glm::mat4x4 lightMatrix;

  etna::GraphicsPipeline staticMeshPipeline{};
  etna::GraphicsPipeline terrainPipeline{};
  etna::ComputePipeline grassPlacementPipeline{};
  etna::GraphicsPipeline grassRenderPipeline{};
  etna::GraphicsPipeline clipmapUpdatePipeline;
  etna::GraphicsPipeline fxaaPipeline{};

  etna::Buffer grassInstanceBuffer;

  etna::Buffer grassIndirectBuffer;


  static constexpr uint32_t maxGrassBlades = 500000;

  static constexpr float grassRadius = 30.0f;

  static constexpr float grassSpacing = 0.15f;

  glm::uvec2 resolution;

  int aaMode = 1;

  char scenePathBuffer[512] = {};
};
