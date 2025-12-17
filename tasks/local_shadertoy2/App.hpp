#pragma once

#include <etna/Window.hpp>
#include <etna/PerFrameCmdMgr.hpp>
#include <etna/ComputePipeline.hpp>
#include <etna/Image.hpp>
#include <etna/Sampler.hpp>
#include <etna/Etna.hpp>
#include <etna/GraphicsPipeline.hpp>
#include <etna/BlockingTransferHelper.hpp>
#include "wsi/OsWindowingManager.hpp"

class App
{
public:
  App();
  ~App();

  void run();

private:
  void drawFrame();
  void update();

private:
  OsWindowingManager windowing;
  std::unique_ptr<OsWindow> osWindow;

  glm::uvec2 resolution;
  bool useVsync;

  std::unique_ptr<etna::Window> vkWindow;
  std::unique_ptr<etna::PerFrameCmdMgr> commandManager;

  etna::GlobalContext* context;
  etna::Image image;
  etna::Image textureImage;
  etna::Sampler defaultSampler;
  etna::Sampler defaultTextureSampler;
  etna::GraphicsPipeline mainPipeline;
  etna::GraphicsPipeline texturePipeline;
};
