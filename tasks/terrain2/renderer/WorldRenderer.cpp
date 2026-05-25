#include "WorldRenderer.hpp"

#include <etna/GlobalContext.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/RenderTargetStates.hpp>
#include <etna/BlockingTransferHelper.hpp>
#include <etna/Profiling.hpp>
#include <etna/Etna.hpp>
#include <glm/ext.hpp>

#include <array>
#include <cmath>
#include <numeric>
#include <random>
#include <vector>
#include <iostream>


namespace
{

class PerlinNoise
{
public:
  PerlinNoise(uint32_t seed = 0)
  {
    std::iota(perm.begin(), perm.end(), 0);
    std::mt19937 rng(seed);
    std::shuffle(perm.begin(), perm.end(), rng);
  }

  float noise(float x, float y) const
  {
    int xi = static_cast<int>(std::floor(x)) & 255;
    int yi = static_cast<int>(std::floor(y)) & 255;

    float xf = x - std::floor(x);
    float yf = y - std::floor(y);

    float u = fade(xf);
    float v = fade(yf);

    int aa = perm[(perm[xi] + yi) & 255];
    int ab = perm[(perm[xi] + yi + 1) & 255];
    int ba = perm[(perm[(xi + 1) & 255] + yi) & 255];
    int bb = perm[(perm[(xi + 1) & 255] + yi + 1) & 255];

    float x1 = lerp(grad(aa, xf, yf), grad(ba, xf - 1, yf), u);
    float x2 = lerp(grad(ab, xf, yf - 1), grad(bb, xf - 1, yf - 1), u);

    return lerp(x1, x2, v);
  }


  float fractal(float x, float y, int octaves, float lacunarity = 2.0f, float persistence = 0.5f)
    const
  {
    float value = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float maxAmplitude = 0.0f;

    for (int i = 0; i < octaves; ++i)
    {
      value += amplitude * noise(x * frequency, y * frequency);
      maxAmplitude += amplitude;
      amplitude *= persistence;
      frequency *= lacunarity;
    }

    return value / maxAmplitude;
  }

private:

  static float fade(float t) { return t * t * t * (t * (t * 6 - 15) + 10); }

  static float lerp(float a, float b, float t) { return a + t * (b - a); }

  static float grad(int hash, float x, float y)
  {
    switch (hash & 3)
    {
    case 0:
      return x + y;
    case 1:
      return -x + y;
    case 2:
      return x - y;
    case 3:
      return -x - y;
    default:
      return 0;
    }
  }

  std::array<uint8_t, 256> perm;
};

}


WorldRenderer::WorldRenderer()
  : sceneMgr{std::make_unique<SceneManager>()}
{
}

void WorldRenderer::allocateResources(glm::uvec2 swapchain_resolution)
{
  resolution = swapchain_resolution;

  auto& ctx = etna::get_context();

  mainViewDepth = ctx.createImage(etna::Image::CreateInfo{
    .extent = vk::Extent3D{resolution.x, resolution.y, 1},
    .name = "main_view_depth",
    .format = vk::Format::eD32Sfloat,
    .imageUsage = vk::ImageUsageFlagBits::eDepthStencilAttachment,
  });

  generateHeightmap();
  generateSplatmap();
  loadDetailTextures();
  allocateClipmapResources();

  grassInstanceBuffer = ctx.createBuffer(etna::Buffer::CreateInfo{
    .size = 16 + maxGrassBlades * 32,
    .bufferUsage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst |
      vk::BufferUsageFlagBits::eTransferSrc,
    .name = "grass_instances",
  });

  grassIndirectBuffer = ctx.createBuffer(etna::Buffer::CreateInfo{
    .size = sizeof(uint32_t) * 4,
    .bufferUsage = vk::BufferUsageFlagBits::eIndirectBuffer |
      vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
    .name = "grass_indirect",
  });
}

void WorldRenderer::loadScene(std::filesystem::path path)
{
  sceneMgr->selectScene(path);
}

void WorldRenderer::generateHeightmap()
{
  std::cout << "Start generating heightmap" << '\n';

  constexpr uint32_t size = 4096;
  constexpr int octaves = 8;
  constexpr float scale = 6.0f;

  PerlinNoise perlin(42);

  heightmapPixels.resize(size * size);
  for (uint32_t y = 0; y < size; ++y)
  {
    for (uint32_t x = 0; x < size; ++x)
    {
      float nx = static_cast<float>(x) / size * scale;
      float ny = static_cast<float>(y) / size * scale;
      float value = perlin.fractal(nx, ny, octaves) * 0.5f + 0.5f;
      heightmapPixels[y * size + x] = value;
    }
  }

  auto& ctx = etna::get_context();

  heightmap = ctx.createImage(etna::Image::CreateInfo{
    .extent = vk::Extent3D{size, size, 1},
    .name = "heightmap",
    .format = vk::Format::eR32Sfloat,
    .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
  });

  heightmapSampler = etna::Sampler(etna::Sampler::CreateInfo{
    .filter = vk::Filter::eLinear,
    .addressMode = vk::SamplerAddressMode::eClampToEdge,
    .name = "heightmap_sampler",
  });

  auto oneShotCmds = ctx.createOneShotCmdMgr();
  etna::BlockingTransferHelper transferHelper(
    etna::BlockingTransferHelper::CreateInfo{.stagingSize = size * size * sizeof(float)});

  std::span<const std::byte> rawData(
    reinterpret_cast<const std::byte*>(heightmapPixels.data()), heightmapPixels.size() * sizeof(float));
  transferHelper.uploadImage(*oneShotCmds, heightmap, 0, 0, rawData);
}

void WorldRenderer::generateSplatmap() 
{
  std::cout << "Start generating splatmap" << '\n';
  uint32_t size = 4096;
  
  std::vector<uint8_t> splatmapPixels(size * size * 4);
  
  for (uint32_t i = 0; i < size * size; ++i)
  {
    float h = heightmapPixels[i];
    
    float gravel = 0.0f;
    float sand = 0.0f;
    float ground = 0.0f;
    float snow = 0.0f;
    
    if (h < 0.35f) {
      gravel = 1.0f;
    }
    else if (h < 0.5f) {
      sand = 1.0f;
    }
    else if (h < 0.65f) {
      ground = 1.0f;
    } else {
      snow = 1.0f;
    }
  
    splatmapPixels[i * 4 + 0] = uint8_t(gravel * 255);
    splatmapPixels[i * 4 + 1] = uint8_t(sand * 255);
    splatmapPixels[i * 4 + 2] = uint8_t(ground * 255);
    splatmapPixels[i * 4 + 3] = uint8_t(snow * 255);
  }

  auto& ctx = etna::get_context();

  splatmap = ctx.createImage(etna::Image::CreateInfo{
    .extent = vk::Extent3D{size, size, 1},
    .name = "splatmap",
    .format = vk::Format::eR8G8B8A8Unorm,
    .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
  });

  splatmapSampler = etna::Sampler(etna::Sampler::CreateInfo{
    .filter = vk::Filter::eLinear,
    .addressMode = vk::SamplerAddressMode::eClampToEdge,
    .name = "splatmap_sampler",
  });

  auto oneShotCmds = ctx.createOneShotCmdMgr();
  etna::BlockingTransferHelper transferHelper(
    etna::BlockingTransferHelper::CreateInfo{.stagingSize = splatmapPixels.size()});

  std::span<const std::byte> rawData(
    reinterpret_cast<const std::byte*>(splatmapPixels.data()), splatmapPixels.size());
  transferHelper.uploadImage(*oneShotCmds, splatmap, 0, 0, rawData);
}

void WorldRenderer::loadDetailTextures()
{
  std::cout << "Start loading textures" << std::endl;
  auto& ctx = etna::get_context();
  auto oneShotCmds = ctx.createOneShotCmdMgr();

  detailSampler = etna::Sampler(etna::Sampler::CreateInfo{
    .filter = vk::Filter::eLinear,
    .addressMode = vk::SamplerAddressMode::eRepeat,
    .name = "detail_sampler",
  });

  const std::string albedoPaths[4] = {
    std::string(GRAPHICS_COURSE_RESOURCES_ROOT) + "/textures/Gravel/color.png",
    std::string(GRAPHICS_COURSE_RESOURCES_ROOT) + "/textures/Sand/color.png",
    std::string(GRAPHICS_COURSE_RESOURCES_ROOT) + "/textures/Ground/color.png",
    std::string(GRAPHICS_COURSE_RESOURCES_ROOT) + "/textures/Snow/color.png",
  };

  const std::string heightPaths[4] = {
    std::string(GRAPHICS_COURSE_RESOURCES_ROOT) + "/textures/Gravel/height.png",
    std::string(GRAPHICS_COURSE_RESOURCES_ROOT) + "/textures/Sand/height.png",
    std::string(GRAPHICS_COURSE_RESOURCES_ROOT) + "/textures/Ground/height.png",
    std::string(GRAPHICS_COURSE_RESOURCES_ROOT) + "/textures/Snow/height.png",
  };

  for (int i = 0; i < 4; ++i)
  {
    {
      int x, y, n;
      unsigned char* data = stbi_load(albedoPaths[i].c_str(), &x, &y, &n, 4);
      std::cout << "Loading: " << albedoPaths[i] << std::endl;
      std::cout << "Result: " << (data ? "OK" : "FAILED") << " size: " << x << "x" << y << std::endl;
      assert(data != nullptr);

      detailTextures[i] = ctx.createImage(etna::Image::CreateInfo{
        .extent = vk::Extent3D{static_cast<uint32_t>(x), static_cast<uint32_t>(y), 1},
        .name = "detail_albedo",
        .format = vk::Format::eR8G8B8A8Unorm,
        .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
      });

      etna::BlockingTransferHelper transferHelper{
        etna::BlockingTransferHelper::CreateInfo{.stagingSize = static_cast<size_t>(x * y * 4)}};
      transferHelper.uploadImage(
        *oneShotCmds,
        detailTextures[i],
        0,
        0,
        std::span<const std::byte>{
          reinterpret_cast<const std::byte*>(data), static_cast<size_t>(x * y * 4)});

      stbi_image_free(data);
    }

    {
      int x, y, n;
      unsigned char* data = stbi_load(heightPaths[i].c_str(), &x, &y, &n, 1);
      assert(data != nullptr);

      detailHeightmaps[i] = ctx.createImage(etna::Image::CreateInfo{
        .extent = vk::Extent3D{static_cast<uint32_t>(x), static_cast<uint32_t>(y), 1},
        .name = "detail_height",
        .format = vk::Format::eR8Unorm,
        .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
      });

      etna::BlockingTransferHelper transferHelper{
        etna::BlockingTransferHelper::CreateInfo{.stagingSize = static_cast<size_t>(x * y * 1)}};
      transferHelper.uploadImage(
        *oneShotCmds,
        detailHeightmaps[i],
        0,
        0,
        std::span<const std::byte>{
          reinterpret_cast<const std::byte*>(data), static_cast<size_t>(x * y * 1)});

      stbi_image_free(data);
    }
  }
}

void WorldRenderer::allocateClipmapResources()
{
  auto& ctx = etna::get_context();

  for (int i = 0; i < CLIPMAP_CASCADES; ++i)
  {
    clipmapImages[i] = ctx.createImage(etna::Image::CreateInfo{
      .extent = vk::Extent3D{CLIPMAP_SIZE, CLIPMAP_SIZE, 1},
      .name = "clipmap_cascade",
      .format = vk::Format::eR8G8B8A8Unorm,
      .imageUsage = vk::ImageUsageFlagBits::eSampled
                  | vk::ImageUsageFlagBits::eColorAttachment,
    });
  }

  clipmapSampler = etna::Sampler(etna::Sampler::CreateInfo{
    .filter = vk::Filter::eLinear,
    .addressMode = vk::SamplerAddressMode::eClampToEdge,
    .name = "clipmap_sampler",
  });
}

void WorldRenderer::updateClipmapCascades(vk::CommandBuffer cmd_buf)
{
  auto programInfo = etna::get_shader_program("clipmap_update");

  for (int cascadeIdx = 0; cascadeIdx < CLIPMAP_CASCADES; ++cascadeIdx)
  {
    auto set = etna::create_descriptor_set(
      programInfo.getDescriptorLayoutId(0),
      cmd_buf,
      {
        etna::Binding{0, heightmap.genBinding(heightmapSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
        etna::Binding{1, splatmap.genBinding(splatmapSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
        etna::Binding{2, detailTextures[0].genBinding(detailSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
        etna::Binding{3, detailTextures[1].genBinding(detailSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
        etna::Binding{4, detailTextures[2].genBinding(detailSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
        etna::Binding{5, detailTextures[3].genBinding(detailSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
        etna::Binding{6, detailHeightmaps[0].genBinding(detailSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
        etna::Binding{7, detailHeightmaps[1].genBinding(detailSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
        etna::Binding{8, detailHeightmaps[2].genBinding(detailSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
        etna::Binding{9, detailHeightmaps[3].genBinding(detailSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
      });

    etna::RenderTargetState renderTarget(
      cmd_buf,
      {{0, 0}, {CLIPMAP_SIZE, CLIPMAP_SIZE}},
      {{.image = clipmapImages[cascadeIdx].get(),
        .view = clipmapImages[cascadeIdx].getView({})}},
      {});

    cmd_buf.bindPipeline(
      vk::PipelineBindPoint::eGraphics, clipmapUpdatePipeline.getVkPipeline());

    cmd_buf.bindDescriptorSets(
      vk::PipelineBindPoint::eGraphics,
      clipmapUpdatePipeline.getVkPipelineLayout(),
      0,
      {set.getVkSet()},
      {});

    ClipmapPushConstants pc{};
    pc.worldSize = cascadeWorldSizes[cascadeIdx];
    pc.cascadeIdx = cascadeIdx;
    pc.clipmapCenter = glm::vec4(clipmapCenterPos.x, 0.0f, clipmapCenterPos.y, 0.0f);

    cmd_buf.pushConstants<ClipmapPushConstants>(
      clipmapUpdatePipeline.getVkPipelineLayout(),
      vk::ShaderStageFlagBits::eFragment,
      0,
      {pc});

    cmd_buf.draw(3, 1, 0, 0);
  }

  for (int i = 0; i < CLIPMAP_CASCADES; ++i)
  {
    etna::set_state(
      cmd_buf,
      clipmapImages[i].get(),
      vk::PipelineStageFlagBits2::eFragmentShader,
      vk::AccessFlagBits2::eShaderSampledRead,
      vk::ImageLayout::eShaderReadOnlyOptimal,
      vk::ImageAspectFlagBits::eColor);
  }

  etna::flush_barriers(cmd_buf);
}

void WorldRenderer::loadShaders()
{

  etna::create_program(
    "static_mesh_material",
    {TERRAIN2_RENDERER_SHADERS_ROOT "static_mesh.frag.spv",
     TERRAIN2_RENDERER_SHADERS_ROOT "static_mesh.vert.spv"});
  etna::create_program("static_mesh", {TERRAIN2_RENDERER_SHADERS_ROOT "static_mesh.vert.spv"});

  etna::create_program(
    "terrain",
    {TERRAIN2_RENDERER_SHADERS_ROOT "terrain.vert.spv",
     TERRAIN2_RENDERER_SHADERS_ROOT "terrain.tesc.spv",
     TERRAIN2_RENDERER_SHADERS_ROOT "terrain.tese.spv",
     TERRAIN2_RENDERER_SHADERS_ROOT "terrain.frag.spv"});


  etna::create_program(
    "grass_placement", {TERRAIN2_RENDERER_SHADERS_ROOT "grass_placement.comp.spv"});

  etna::create_program(
    "grass_render",
    {TERRAIN2_RENDERER_SHADERS_ROOT "grass.vert.spv",
     TERRAIN2_RENDERER_SHADERS_ROOT "grass.frag.spv"});

  etna::create_program(
    "clipmap_update",
    {TERRAIN2_RENDERER_SHADERS_ROOT "clipmap_update.vert.spv",
    TERRAIN2_RENDERER_SHADERS_ROOT "clipmap_update.frag.spv"});
}

void WorldRenderer::setupPipelines(vk::Format swapchain_format)
{
  auto& pipelineManager = etna::get_context().getPipelineManager();

  terrainPipeline = {};
  terrainPipeline = pipelineManager.createGraphicsPipeline(
    "terrain",
    etna::GraphicsPipeline::CreateInfo{
      .inputAssemblyConfig =
        vk::PipelineInputAssemblyStateCreateInfo{
          .topology = vk::PrimitiveTopology::ePatchList,
        },
      .tessellationConfig =
        vk::PipelineTessellationStateCreateInfo{
          .patchControlPoints = 4,
        },
      .rasterizationConfig =
        vk::PipelineRasterizationStateCreateInfo{
          .polygonMode = vk::PolygonMode::eFill,
          .cullMode = vk::CullModeFlagBits::eNone,
          .frontFace = vk::FrontFace::eCounterClockwise,
          .lineWidth = 1.f,
        },
      .fragmentShaderOutput =
        {
          .colorAttachmentFormats = {swapchain_format},
          .depthAttachmentFormat = vk::Format::eD32Sfloat,
        },
    });


  grassPlacementPipeline = {};
  grassPlacementPipeline = pipelineManager.createComputePipeline("grass_placement", {});

  grassRenderPipeline = {};
  grassRenderPipeline = pipelineManager.createGraphicsPipeline(
    "grass_render",
    etna::GraphicsPipeline::CreateInfo{
      .inputAssemblyConfig =
        vk::PipelineInputAssemblyStateCreateInfo{
          .topology = vk::PrimitiveTopology::eTriangleStrip,
        },
      .rasterizationConfig =
        vk::PipelineRasterizationStateCreateInfo{
          .polygonMode = vk::PolygonMode::eFill,
          .cullMode = vk::CullModeFlagBits::eNone,
          .frontFace = vk::FrontFace::eCounterClockwise,
          .lineWidth = 1.f,
        },
      .fragmentShaderOutput =
        {
          .colorAttachmentFormats = {swapchain_format},
          .depthAttachmentFormat = vk::Format::eD32Sfloat,
        },
    });

  clipmapUpdatePipeline = pipelineManager.createGraphicsPipeline(
    "clipmap_update",
    etna::GraphicsPipeline::CreateInfo{
      .fragmentShaderOutput = {
        .colorAttachmentFormats = {vk::Format::eR8G8B8A8Unorm},
      },
    });
}

void WorldRenderer::debugInput(const Keyboard&) {}

void WorldRenderer::update(const FramePacket& packet)
{
  ZoneScoped;

  {
    const float aspect = float(resolution.x) / float(resolution.y);
    worldViewProj = packet.mainCam.projTm(aspect) * packet.mainCam.viewTm();
    cameraPosition = packet.mainCam.position;
    currentTime = packet.currentTime;
  }
}

void WorldRenderer::renderScene(
  vk::CommandBuffer cmd_buf, const glm::mat4x4& glob_tm, vk::PipelineLayout pipeline_layout)
{
  if (!sceneMgr->getVertexBuffer())
    return;

  cmd_buf.bindVertexBuffers(0, {sceneMgr->getVertexBuffer()}, {0});
  cmd_buf.bindIndexBuffer(sceneMgr->getIndexBuffer(), 0, vk::IndexType::eUint32);

  pushConst2M.projView = glob_tm;

  auto instanceMeshes = sceneMgr->getInstanceMeshes();
  auto instanceMatrices = sceneMgr->getInstanceMatrices();

  auto meshes = sceneMgr->getMeshes();
  auto relems = sceneMgr->getRenderElements();

  for (std::size_t instIdx = 0; instIdx < instanceMeshes.size(); ++instIdx)
  {
    pushConst2M.model = instanceMatrices[instIdx];

    cmd_buf.pushConstants<PushConstants>(
      pipeline_layout, vk::ShaderStageFlagBits::eVertex, 0, {pushConst2M});

    const auto meshIdx = instanceMeshes[instIdx];

    for (std::size_t j = 0; j < meshes[meshIdx].relemCount; ++j)
    {
      const auto relemIdx = meshes[meshIdx].firstRelem + j;
      const auto& relem = relems[relemIdx];
      cmd_buf.drawIndexed(relem.indexCount, 1, relem.indexOffset, relem.vertexOffset, 0);
    }
  }
}

void WorldRenderer::renderTerrain(
  vk::CommandBuffer cmd_buf, vk::DescriptorSet descriptorSet)
{
  cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics, terrainPipeline.getVkPipeline());

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eGraphics,
    terrainPipeline.getVkPipelineLayout(),
    0,
    {descriptorSet},
    {});

  constexpr float terrainWorldSize = 100.0f;
  constexpr int chunkCount = 20;
  constexpr float chunkSize = terrainWorldSize / chunkCount;

  for (int cy = 0; cy < chunkCount; ++cy)
  {
    for (int cx = 0; cx < chunkCount; ++cx)
    {
      TerrainPushConstants pc{};
      pc.projView = worldViewProj;
      pc.chunkOffset = glm::vec4(cx * chunkSize, cy * chunkSize, chunkSize, chunkSize);
      pc.camPos = glm::vec4(cameraPosition, 0.0f);
      pc.clipmapCenter = glm::vec4(clipmapCenterPos.x, 0.0f, clipmapCenterPos.y, 0.0f);

      cmd_buf.pushConstants<TerrainPushConstants>(
        terrainPipeline.getVkPipelineLayout(),
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eTessellationControl |
          vk::ShaderStageFlagBits::eTessellationEvaluation | vk::ShaderStageFlagBits::eFragment,
        0,
        {pc});

      cmd_buf.draw(4, 1, 0, 0);
    }
  }
}

void WorldRenderer::dispatchGrassPlacement(vk::CommandBuffer cmd_buf)
{
  cmd_buf.fillBuffer(grassInstanceBuffer.get(), 0, 4, 0);

  uint32_t indirectData[4] = {7, 0, 0, 0};
  cmd_buf.updateBuffer(grassIndirectBuffer.get(), 0, sizeof(indirectData), indirectData);

  etna::set_state(
    cmd_buf,
    grassInstanceBuffer.get(),
    vk::PipelineStageFlagBits2::eComputeShader,
    vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite);
  etna::set_state(
    cmd_buf,
    grassIndirectBuffer.get(),
    vk::PipelineStageFlagBits2::eComputeShader,
    vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite);
  etna::flush_barriers(cmd_buf);

  cmd_buf.bindPipeline(vk::PipelineBindPoint::eCompute, grassPlacementPipeline.getVkPipeline());

  auto programInfo = etna::get_shader_program("grass_placement");
  auto set = etna::create_descriptor_set(
    programInfo.getDescriptorLayoutId(0),
    cmd_buf,
    {etna::Binding{0, grassInstanceBuffer.genBinding()},
     etna::Binding{
       1,
       heightmap.genBinding(heightmapSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)}});

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eCompute,
    grassPlacementPipeline.getVkPipelineLayout(),
    0,
    {set.getVkSet()},
    {});

  GrassPlacementPushConstants pc{};
  pc.camPos = glm::vec4(cameraPosition, 0.0f);
  pc.time = currentTime;
  pc.radius = grassRadius;
  pc.spacing = grassSpacing;
  pc.terrainSize = 100.0f;

  cmd_buf.pushConstants<GrassPlacementPushConstants>(
    grassPlacementPipeline.getVkPipelineLayout(),
    vk::ShaderStageFlagBits::eCompute,
    0,
    {pc});

  int gridSize = static_cast<int>(2.0f * grassRadius / grassSpacing);
  uint32_t groupsX = (gridSize + 7) / 8;
  uint32_t groupsY = (gridSize + 7) / 8;
  cmd_buf.dispatch(groupsX, groupsY, 1);

  etna::set_state(
    cmd_buf,
    grassInstanceBuffer.get(),
    vk::PipelineStageFlagBits2::eTransfer,
    vk::AccessFlagBits2::eTransferRead);
  etna::flush_barriers(cmd_buf);

  vk::BufferCopy copyRegion{
    .srcOffset = 0,
    .dstOffset = 4,
    .size = 4,
  };
  cmd_buf.copyBuffer(grassInstanceBuffer.get(), grassIndirectBuffer.get(), {copyRegion});

  etna::set_state(
    cmd_buf,
    grassInstanceBuffer.get(),
    vk::PipelineStageFlagBits2::eVertexShader,
    vk::AccessFlagBits2::eShaderStorageRead);
  etna::set_state(
    cmd_buf,
    grassIndirectBuffer.get(),
    vk::PipelineStageFlagBits2::eDrawIndirect,
    vk::AccessFlagBits2::eIndirectCommandRead);
  etna::flush_barriers(cmd_buf);
}

void WorldRenderer::renderGrass(vk::CommandBuffer cmd_buf)
{
  cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics, grassRenderPipeline.getVkPipeline());

  auto programInfo = etna::get_shader_program("grass_render");
  auto set = etna::create_descriptor_set(
    programInfo.getDescriptorLayoutId(0),
    cmd_buf,
    {etna::Binding{0, grassInstanceBuffer.genBinding()}});

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eGraphics,
    grassRenderPipeline.getVkPipelineLayout(),
    0,
    {set.getVkSet()},
    {});

  GrassRenderPushConstants pc{};
  pc.projView = worldViewProj;
  pc.camPos = glm::vec4(cameraPosition, 0.0f);
  pc.time = currentTime;

  cmd_buf.pushConstants<GrassRenderPushConstants>(
    grassRenderPipeline.getVkPipelineLayout(),
    vk::ShaderStageFlagBits::eVertex,
    0,
    {pc});

  cmd_buf.drawIndirect(grassIndirectBuffer.get(), 0, 1, sizeof(uint32_t) * 4);
}

void WorldRenderer::renderWorld(
  vk::CommandBuffer cmd_buf, vk::Image target_image, vk::ImageView target_image_view)
{
  ETNA_PROFILE_GPU(cmd_buf, renderWorld);

  float dist = glm::length(glm::vec2(cameraPosition.x, cameraPosition.z) - lastUpdatePos);
  if (dist > CLIPMAP_UPDATE_THRESHOLD)
  {
    clipmapCenterPos = glm::vec2(cameraPosition.x, cameraPosition.z);
    lastUpdatePos = clipmapCenterPos;
    updateClipmapCascades(cmd_buf);
  }

  auto terrainProgramInfo = etna::get_shader_program("terrain");
  auto terrainSet = etna::create_descriptor_set(
    terrainProgramInfo.getDescriptorLayoutId(0),
    cmd_buf,
    {
      etna::Binding{0, heightmap.genBinding(heightmapSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
      etna::Binding{1, clipmapImages[0].genBinding(clipmapSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
      etna::Binding{2, clipmapImages[1].genBinding(clipmapSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
      etna::Binding{3, clipmapImages[2].genBinding(clipmapSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
      etna::Binding{4, clipmapImages[3].genBinding(clipmapSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
    });

  etna::flush_barriers(cmd_buf);

  // dispatchGrassPlacement(cmd_buf);
  {
    ETNA_PROFILE_GPU(cmd_buf, renderForward);

    etna::RenderTargetState renderTargets(
      cmd_buf,
      {{0, 0}, {resolution.x, resolution.y}},
      {{.image = target_image, .view = target_image_view}},
      {.image = mainViewDepth.get(), .view = mainViewDepth.getView({})});

    renderTerrain(cmd_buf, terrainSet.getVkSet());
    // renderGrass(cmd_buf);
  }
}
