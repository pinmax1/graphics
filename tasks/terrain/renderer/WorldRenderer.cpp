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


namespace
{

// Классическая реализация 2D шума Перлина.
// Используется для процедурной генерации карты высот террейна.
// Алгоритм: для каждой точки (x,y) находим 4 ближайших узла решётки,
// вычисляем градиенты в этих узлах и интерполируем с помощью fade-функции
// (5-я степень Кена Перлина: 6t^5 - 15t^4 + 10t^3).
class PerlinNoise
{
public:
  // seed определяет случайную перестановку — разные seed дают разный ландшафт
  PerlinNoise(uint32_t seed = 0)
  {
    // Заполняем таблицу перестановок [0..255] и перемешиваем
    std::iota(perm.begin(), perm.end(), 0);
    std::mt19937 rng(seed);
    std::shuffle(perm.begin(), perm.end(), rng);
  }

  // Базовый шум Перлина в точке (x, y). Возвращает значение примерно в [-1, 1].
  float noise(float x, float y) const
  {
    // Целая часть координат — индекс ячейки решётки (модуль 256 для зацикливания)
    int xi = static_cast<int>(std::floor(x)) & 255;
    int yi = static_cast<int>(std::floor(y)) & 255;

    // Дробная часть — положение внутри ячейки [0, 1)
    float xf = x - std::floor(x);
    float yf = y - std::floor(y);

    // Fade-функция для плавной интерполяции (убирает артефакты линейной интерполяции)
    float u = fade(xf);
    float v = fade(yf);

    // Хеши для 4 углов ячейки через таблицу перестановок
    int aa = perm[(perm[xi] + yi) & 255];
    int ab = perm[(perm[xi] + yi + 1) & 255];
    int ba = perm[(perm[(xi + 1) & 255] + yi) & 255];
    int bb = perm[(perm[(xi + 1) & 255] + yi + 1) & 255];

    // Интерполяция градиентов: сначала по X, потом по Y
    float x1 = lerp(grad(aa, xf, yf), grad(ba, xf - 1, yf), u);
    float x2 = lerp(grad(ab, xf, yf - 1), grad(bb, xf - 1, yf - 1), u);

    return lerp(x1, x2, v);
  }

  // Многооктавный (фрактальный) шум — суммирует несколько слоёв базового шума
  // с уменьшающейся амплитудой и увеличивающейся частотой.
  // octaves — количество слоёв (8 даёт хорошую детализацию)
  // lacunarity — множитель частоты между октавами (обычно 2.0)
  // persistence — множитель амплитуды между октавами (обычно 0.5)
  float fractal(float x, float y, int octaves, float lacunarity = 2.0f, float persistence = 0.5f)
    const
  {
    float value = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float maxAmplitude = 0.0f; // Для нормализации результата в [-1, 1]

    for (int i = 0; i < octaves; ++i)
    {
      value += amplitude * noise(x * frequency, y * frequency);
      maxAmplitude += amplitude;
      amplitude *= persistence;  // Каждая октава тише предыдущей
      frequency *= lacunarity;   // Каждая октава детальнее предыдущей
    }

    return value / maxAmplitude;
  }

private:
  // Fade-функция Кена Перлина: 6t^5 - 15t^4 + 10t^3
  // Обеспечивает C2-непрерывность (нет видимых швов между ячейками)
  static float fade(float t) { return t * t * t * (t * (t * 6 - 15) + 10); }

  static float lerp(float a, float b, float t) { return a + t * (b - a); }

  // Псевдослучайный градиент в узле решётки.
  // По хешу выбирает один из 4 направлений и возвращает скалярное произведение
  // с вектором от узла к точке (x, y).
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

  // Таблица перестановок — определяет "случайность" шума
  std::array<uint8_t, 256> perm;
};

} // namespace


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

  // Генерируем и загружаем heightmap на GPU при старте
  generateHeightmap();
}

void WorldRenderer::loadScene(std::filesystem::path path)
{
  sceneMgr->selectScene(path);
}

// Генерирует карту высот 4096x4096 многооктавным шумом Перлина
// и загружает на GPU через staging buffer.
void WorldRenderer::generateHeightmap()
{
  constexpr uint32_t size = 4096; // Разрешение heightmap (4096x4096 текселей)
  constexpr int octaves = 8;      // Количество октав шума (больше = детальнее)
  constexpr float scale = 6.0f;   // Масштаб шума в текстурном пространстве
                                   // (больше = более крупные холмы)

  PerlinNoise perlin(42); // Фиксированный seed для воспроизводимости

  // Генерируем пиксели heightmap на CPU
  std::vector<float> pixels(size * size);
  for (uint32_t y = 0; y < size; ++y)
  {
    for (uint32_t x = 0; x < size; ++x)
    {
      // Нормализуем координаты пикселя в [0, scale] для семплирования шума
      float nx = static_cast<float>(x) / size * scale;
      float ny = static_cast<float>(y) / size * scale;
      // fractal возвращает [-1, 1], преобразуем в [0, 1] для хранения в текстуре
      float value = perlin.fractal(nx, ny, octaves) * 0.5f + 0.5f;
      pixels[y * size + x] = value;
    }
  }

  auto& ctx = etna::get_context();

  // Создаём GPU-образ: R32_SFLOAT = одноканальный float32.
  // eSampled — будет читаться в шейдерах через sampler2D.
  // eTransferDst — чтобы можно было загрузить данные через transfer.
  heightmap = ctx.createImage(etna::Image::CreateInfo{
    .extent = vk::Extent3D{size, size, 1},
    .name = "heightmap",
    .format = vk::Format::eR32Sfloat,
    .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
  });

  // Сэмплер с билинейной фильтрацией — интерполирует между текселями heightmap
  // при семплировании в TESE, давая плавную поверхность.
  heightmapSampler = etna::Sampler(etna::Sampler::CreateInfo{
    .filter = vk::Filter::eLinear,
    .addressMode = vk::SamplerAddressMode::eClampToEdge,
    .name = "heightmap_sampler",
  });

  // Загружаем данные на GPU через blocking transfer (staging buffer -> image).
  // Используется только при инициализации, поэтому blocking допустим.
  auto oneShotCmds = ctx.createOneShotCmdMgr();
  etna::BlockingTransferHelper transferHelper(
    etna::BlockingTransferHelper::CreateInfo{.stagingSize = size * size * sizeof(float)});

  std::span<const std::byte> rawData(
    reinterpret_cast<const std::byte*>(pixels.data()), pixels.size() * sizeof(float));
  transferHelper.uploadImage(*oneShotCmds, heightmap, 0, 0, rawData);
}

void WorldRenderer::loadShaders()
{
  // Шейдерная программа для отрисовки моделей (оставлена из model_bakery)
  etna::create_program(
    "static_mesh_material",
    {TERRAIN_RENDERER_SHADERS_ROOT "static_mesh.frag.spv",
     TERRAIN_RENDERER_SHADERS_ROOT "static_mesh.vert.spv"});
  etna::create_program("static_mesh", {TERRAIN_RENDERER_SHADERS_ROOT "static_mesh.vert.spv"});

  // Шейдерная программа террейна: все 4 стадии тесселляционного пайплайна.
  // vert -> tesc -> tese -> frag
  etna::create_program(
    "terrain",
    {TERRAIN_RENDERER_SHADERS_ROOT "terrain.vert.spv",
     TERRAIN_RENDERER_SHADERS_ROOT "terrain.tesc.spv",
     TERRAIN_RENDERER_SHADERS_ROOT "terrain.tese.spv",
     TERRAIN_RENDERER_SHADERS_ROOT "terrain.frag.spv"});
}

void WorldRenderer::setupPipelines(vk::Format swapchain_format)
{
  auto& pipelineManager = etna::get_context().getPipelineManager();

  // Пайплайн террейна с тесселляцией
  terrainPipeline = {};
  terrainPipeline = pipelineManager.createGraphicsPipeline(
    "terrain",
    etna::GraphicsPipeline::CreateInfo{
      // ePatchList — специальная топология для тесселляции.
      // Вершины группируются в патчи, а не в треугольники.
      .inputAssemblyConfig =
        vk::PipelineInputAssemblyStateCreateInfo{
          .topology = vk::PrimitiveTopology::ePatchList,
        },
      // 4 контрольные точки на патч = квад (4 угла чанка)
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
}

void WorldRenderer::debugInput(const Keyboard&) {}

void WorldRenderer::update(const FramePacket& packet)
{
  ZoneScoped;

  {
    const float aspect = float(resolution.x) / float(resolution.y);
    worldViewProj = packet.mainCam.projTm(aspect) * packet.mainCam.viewTm();
    cameraPosition = packet.mainCam.position;
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

// Рисует террейн как сетку chunkCount x chunkCount чанков.
// Каждый чанк — один draw(4, 1, 0, 0): 4 вершины образуют один quad-патч,
// который тесселляционные шейдеры разбивают на сетку треугольников.
void WorldRenderer::renderTerrain(
  vk::CommandBuffer cmd_buf, vk::DescriptorSet descriptorSet)
{
  cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics, terrainPipeline.getVkPipeline());

  // Биндим descriptor set с heightmap текстурой (set=0, binding=0)
  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eGraphics,
    terrainPipeline.getVkPipelineLayout(),
    0,
    {descriptorSet},
    {});

  // Размер террейна в мировых единицах (должен совпадать с terrainWorldSize в TESE)
  constexpr float terrainWorldSize = 100.0f;
  // Количество чанков по каждой оси. Больше чанков = точнее LOD-тесселляция,
  constexpr int chunkCount = 1;
  constexpr float chunkSize = terrainWorldSize / chunkCount;

  for (int cy = 0; cy < chunkCount; ++cy)
  {
    for (int cx = 0; cx < chunkCount; ++cx)
    {
      TerrainPushConstants pc{};
      pc.projView = worldViewProj;
      // chunkOffset: xy = мировое смещение левого нижнего угла, zw = размер чанка
      pc.chunkOffset = glm::vec4(cx * chunkSize, cy * chunkSize, chunkSize, chunkSize);
      pc.camPos = glm::vec4(cameraPosition, 0.0f);

      // Push constants доступны во всех 4 стадиях шейдеров
      cmd_buf.pushConstants<TerrainPushConstants>(
        terrainPipeline.getVkPipelineLayout(),
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eTessellationControl |
          vk::ShaderStageFlagBits::eTessellationEvaluation | vk::ShaderStageFlagBits::eFragment,
        0,
        {pc});

      // 4 вершины = 1 патч (квад). Тесселлятор разобьёт его на треугольники.
      cmd_buf.draw(4, 1, 0, 0);
    }
  }
}

void WorldRenderer::renderWorld(
  vk::CommandBuffer cmd_buf, vk::Image target_image, vk::ImageView target_image_view)
{
  ETNA_PROFILE_GPU(cmd_buf, renderWorld);

  // Создаём descriptor set для heightmap и выполняем image layout transition
  // (eTransferDst -> eShaderReadOnlyOptimal) ДО начала render pass,
  // т.к. Vulkan запрещает image barriers внутри dynamic rendering.
  auto programInfo = etna::get_shader_program("terrain");
  auto set = etna::create_descriptor_set(
    programInfo.getDescriptorLayoutId(0),
    cmd_buf,
    {etna::Binding{
      0,
      heightmap.genBinding(heightmapSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)}});

  etna::flush_barriers(cmd_buf);

  // Render pass: рисуем террейн в swapchain image с depth buffer
  {
    ETNA_PROFILE_GPU(cmd_buf, renderForward);

    etna::RenderTargetState renderTargets(
      cmd_buf,
      {{0, 0}, {resolution.x, resolution.y}},
      {{.image = target_image, .view = target_image_view}},
      {.image = mainViewDepth.get(), .view = mainViewDepth.getView({})});

    renderTerrain(cmd_buf, set.getVkSet());
  }
}
