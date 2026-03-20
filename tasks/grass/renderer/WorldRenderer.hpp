#pragma once

#include <etna/Image.hpp>
#include <etna/Sampler.hpp>
#include <etna/Buffer.hpp>
#include <etna/GraphicsPipeline.hpp>
#include <etna/ComputePipeline.hpp>
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

  void generateHeightmap();
  void renderTerrain(vk::CommandBuffer cmd_buf, vk::DescriptorSet descriptorSet);
  void dispatchGrassPlacement(vk::CommandBuffer cmd_buf);
  void renderGrass(vk::CommandBuffer cmd_buf);

private:
  std::unique_ptr<SceneManager> sceneMgr;

  etna::Image mainViewDepth;
  etna::Buffer constants;

  etna::Image heightmap;
  etna::Sampler heightmapSampler;

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

  glm::mat4x4 worldViewProj;
  glm::vec3 cameraPosition;
  float currentTime = 0.0f;
  glm::mat4x4 lightMatrix;

  etna::GraphicsPipeline staticMeshPipeline{};
  etna::GraphicsPipeline terrainPipeline{};
  etna::ComputePipeline grassPlacementPipeline{};
  etna::GraphicsPipeline grassRenderPipeline{};

  etna::Buffer grassInstanceBuffer;

  etna::Buffer grassIndirectBuffer;


  static constexpr uint32_t maxGrassBlades = 500000;

  static constexpr float grassRadius = 30.0f;

  static constexpr float grassSpacing = 0.15f;

  glm::uvec2 resolution;
};
