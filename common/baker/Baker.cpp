#include <filesystem>
#include <glm/glm.hpp>
#include <spdlog/spdlog.h>
#include <optional>
#include "tiny_gltf.h"
#include "Baker.hpp"

std::uint32_t encode_normal(glm::vec3 normal)
{
  const int32_t x = (std::lround(normal.x * 127.0f) & 0x000000ff);
  const int32_t y = (std::lround(normal.y * 127.0f) & 0x000000ff) << 8;
  const int32_t z = (std::lround(normal.z * 127.0f) & 0x000000ff) << 16;
  const int32_t w = (127 & 0x000000ff) << 24;

  const int32_t res = x | y | z | w;

  return std::bit_cast<std::uint32_t>(res);
}



std::optional<tinygltf::Model> loadModel(std::filesystem::path path)
{
  tinygltf::TinyGLTF loader;
  loader.SetImagesAsIs(true);
  tinygltf::Model model;

  std::string error;
  std::string warning;
  bool success = false;

  auto ext = path.extension();
  if (ext == ".gltf")
    success = loader.LoadASCIIFromFile(&model, &error, &warning, path.string());
  else if (ext == ".glb")
    success = loader.LoadBinaryFromFile(&model, &error, &warning, path.string());
  else
  {
    spdlog::error("glTF: Unknown glTF file extension. Expected .gltf or .glb.");
    return std::nullopt;
  }

  if (!success)
  {
    spdlog::error("glTF: Failed to load model!");
    if (!error.empty())
      spdlog::error("glTF: {}", error);
    return std::nullopt;
  }

  if (!warning.empty())
    spdlog::warn("glTF: {}", warning);

  if (
    !model.extensions.empty() || !model.extensionsRequired.empty() || !model.extensionsUsed.empty())
    spdlog::warn("glTF: No glTF extensions are currently implemented!");

  return model;
}

int saveModel(tinygltf::Model& model, std::filesystem::path& path) 
{
  tinygltf::TinyGLTF saver;
  saver.SetImagesAsIs(true);
  bool success = false;

  success = saver.WriteGltfSceneToFile(&model, path.string(), 0, 0, 1, 0);
  if (!success)
  {
    spdlog::error("glTF: Failed to save model!");
    return 1;
  }
  return 0;
}



ProcessedMeshes processMeshes(const tinygltf::Model& model)
{
  // NOTE: glTF assets can have pretty wonky data layouts which are not appropriate
  // for real-time rendering, so we have to press the data first. In serious engines
  // this is mitigated by storing assets on the disc in an engine-specific format that
  // is appropriate for GPU upload right after reading from disc.

  ProcessedMeshes result;

  // Pre-allocate enough memory so as not to hit the
  // allocator on the memcpy hotpath
  {
    size_t vertexBytes = 0;
    size_t indexBytes = 0;
    for (const auto& bufView : model.bufferViews)
    {
      switch (bufView.target)
      {
      case TINYGLTF_TARGET_ARRAY_BUFFER:
        vertexBytes += bufView.byteLength;
        break;
      case TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER:
        indexBytes += bufView.byteLength;
        break;
      default:
        break;
      }
    }
    result.vertices.reserve(vertexBytes / sizeof(Vertex));
    result.indices.reserve(indexBytes / sizeof(std::uint32_t));
  }

  {
    size_t totalPrimitives = 0;
    for (const auto& mesh : model.meshes)
      totalPrimitives += mesh.primitives.size();
    result.relems.reserve(totalPrimitives);
  }

  result.meshes.reserve(model.meshes.size());

  for (const auto& mesh : model.meshes)
  {
    result.meshes.push_back(Mesh{
      .firstRelem = static_cast<std::uint32_t>(result.relems.size()),
      .relemCount = static_cast<std::uint32_t>(mesh.primitives.size()),
    });

    for (const auto& prim : mesh.primitives)
    {
      if (prim.mode != TINYGLTF_MODE_TRIANGLES)
      {
        spdlog::warn(
          "Encountered a non-triangles primitive, these are not supported for now, skipping it!");
        --result.meshes.back().relemCount;
        continue;
      }

      const auto normalIt = prim.attributes.find("NORMAL");
      const auto tangentIt = prim.attributes.find("TANGENT");
      const auto texcoordIt = prim.attributes.find("TEXCOORD_0");

      const bool hasNormals = normalIt != prim.attributes.end();
      const bool hasTangents = tangentIt != prim.attributes.end();
      const bool hasTexcoord = texcoordIt != prim.attributes.end();
      std::array accessorIndices{
        prim.indices,
        prim.attributes.at("POSITION"),
        hasNormals ? normalIt->second : -1,
        hasTangents ? tangentIt->second : -1,
        hasTexcoord ? texcoordIt->second : -1,
      };

      std::array accessors{
        &model.accessors[prim.indices],
        &model.accessors[accessorIndices[1]],
        hasNormals ? &model.accessors[accessorIndices[2]] : nullptr,
        hasTangents ? &model.accessors[accessorIndices[3]] : nullptr,
        hasTexcoord ? &model.accessors[accessorIndices[4]] : nullptr,
      };

      std::array bufViews{
        &model.bufferViews[accessors[0]->bufferView],
        &model.bufferViews[accessors[1]->bufferView],
        hasNormals ? &model.bufferViews[accessors[2]->bufferView] : nullptr,
        hasTangents ? &model.bufferViews[accessors[3]->bufferView] : nullptr,
        hasTexcoord ? &model.bufferViews[accessors[4]->bufferView] : nullptr,
      };

      result.relems.push_back(RenderElement{
        .vertexOffset = static_cast<std::uint32_t>(result.vertices.size()),
        .indexOffset = static_cast<std::uint32_t>(result.indices.size()),
        .indexCount = static_cast<std::uint32_t>(accessors[0]->count),
      });

      const size_t vertexCount = accessors[1]->count;

      std::array ptrs{
        reinterpret_cast<const std::byte*>(model.buffers[bufViews[0]->buffer].data.data()) +
          bufViews[0]->byteOffset + accessors[0]->byteOffset,
        reinterpret_cast<const std::byte*>(model.buffers[bufViews[1]->buffer].data.data()) +
          bufViews[1]->byteOffset + accessors[1]->byteOffset,
        hasNormals
          ? reinterpret_cast<const std::byte*>(model.buffers[bufViews[2]->buffer].data.data()) +
            bufViews[2]->byteOffset + accessors[2]->byteOffset
          : nullptr,
        hasTangents
          ? reinterpret_cast<const std::byte*>(model.buffers[bufViews[3]->buffer].data.data()) +
            bufViews[3]->byteOffset + accessors[3]->byteOffset
          : nullptr,
        hasTexcoord
          ? reinterpret_cast<const std::byte*>(model.buffers[bufViews[4]->buffer].data.data()) +
            bufViews[4]->byteOffset + accessors[4]->byteOffset
          : nullptr,
      };

      std::array strides{
        bufViews[0]->byteStride != 0
          ? bufViews[0]->byteStride
          : tinygltf::GetComponentSizeInBytes(accessors[0]->componentType) *
            tinygltf::GetNumComponentsInType(accessors[0]->type),
        bufViews[1]->byteStride != 0
          ? bufViews[1]->byteStride
          : tinygltf::GetComponentSizeInBytes(accessors[1]->componentType) *
            tinygltf::GetNumComponentsInType(accessors[1]->type),
        hasNormals ? (bufViews[2]->byteStride != 0
                        ? bufViews[2]->byteStride
                        : tinygltf::GetComponentSizeInBytes(accessors[2]->componentType) *
                          tinygltf::GetNumComponentsInType(accessors[2]->type))
                   : 0,
        hasTangents ? (bufViews[3]->byteStride != 0
                         ? bufViews[3]->byteStride
                         : tinygltf::GetComponentSizeInBytes(accessors[3]->componentType) *
                           tinygltf::GetNumComponentsInType(accessors[3]->type))
                    : 0,
        hasTexcoord ? (bufViews[4]->byteStride != 0
                         ? bufViews[4]->byteStride
                         : tinygltf::GetComponentSizeInBytes(accessors[4]->componentType) *
                           tinygltf::GetNumComponentsInType(accessors[4]->type))
                    : 0,
      };

      for (size_t i = 0; i < vertexCount; ++i)
      {
        auto& vtx = result.vertices.emplace_back();
        glm::vec3 pos;
        // Fall back to 0 in case we don't have something.
        // NOTE: if tangents are not available, one could use http://mikktspace.com/
        // NOTE: if normals are not available, reconstructing them is possible but will look ugly
        glm::vec3 normal{0};
        glm::vec3 tangent{0};
        glm::vec2 texcoord{0};
        std::memcpy(&pos, ptrs[1], sizeof(pos));

        // NOTE: it's faster to do a template here with specializations for all combinations than to
        // do ifs at runtime. Also, SIMD should be used. Try implementing this!
        if (hasNormals)
          std::memcpy(&normal, ptrs[2], sizeof(normal));
        if (hasTangents)
          std::memcpy(&tangent, ptrs[3], sizeof(tangent));
        if (hasTexcoord)
          std::memcpy(&texcoord, ptrs[4], sizeof(texcoord));


        vtx.positionAndNormal = glm::vec4(pos, std::bit_cast<float>(encode_normal(normal)));
        vtx.texCoordAndTangentAndPadding =
          glm::vec4(texcoord, std::bit_cast<float>(encode_normal(tangent)), 0);

        ptrs[1] += strides[1];
        if (hasNormals)
          ptrs[2] += strides[2];
        if (hasTangents)
          ptrs[3] += strides[3];
        if (hasTexcoord)
          ptrs[4] += strides[4];
      }

      // Indices are guaranteed to have no stride
      const size_t indexCount = accessors[0]->count;
      if (accessors[0]->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
      {
        for (size_t i = 0; i < indexCount; ++i)
        {
          std::uint16_t index;
          std::memcpy(&index, ptrs[0], sizeof(index));
          result.indices.push_back(index);
          ptrs[0] += 2;
        }
      }
      else if (accessors[0]->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
      {
        const size_t lastTotalIndices = result.indices.size();
        result.indices.resize(lastTotalIndices + indexCount);
        std::memcpy(
          result.indices.data() + lastTotalIndices,
          ptrs[0],
          sizeof(result.indices[0]) * indexCount);
      }
    }
  }

  return result;
}



int bakeModel(std::filesystem::path modelPath)
{
  std::filesystem::path bakedModelPath;
  std::filesystem::path bakedBinPath;
  {
      const std::filesystem::path modelDir  = modelPath.parent_path();
      const std::filesystem::path modelName = modelPath.stem();
      bakedModelPath = modelDir / (modelName.string() + "_baked.gltf");
      bakedBinPath   = modelDir / (modelName.string() + "_baked.bin");
  }

  auto maybeModel = loadModel(modelPath);
  if (!maybeModel.has_value())
    return 1;
  tinygltf::Model model = std::move(*maybeModel);

  auto bakedModel = processMeshes(model);

  size_t indiciesSize = bakedModel.indices.size() * sizeof(uint32_t);
  size_t verticesOffset = (indiciesSize + 15) / 16 * 16;
  size_t verticesSize = bakedModel.vertices.size() * sizeof(Vertex);

  {
    tinygltf::Buffer buffer;
    buffer.name = bakedBinPath.stem().string();
    buffer.uri = bakedBinPath.filename().string();
    buffer.data.resize(verticesOffset + verticesSize);

    memcpy(buffer.data.data(), bakedModel.indices.data(), indiciesSize);
    memcpy(buffer.data.data() + verticesOffset, bakedModel.vertices.data(), verticesSize);

    model.buffers.clear();
    model.buffers.push_back(std::move(buffer));
  }

  {
    tinygltf::BufferView bufferViews[2]{};

    bufferViews[0].name = "baked_indicies";
    bufferViews[0].buffer = 0;
    bufferViews[0].byteOffset = 0;
    bufferViews[0].byteLength = indiciesSize;
    bufferViews[0].byteStride = 0;
    bufferViews[0].target = TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER;

    bufferViews[1].name = "baked_vertices";
    bufferViews[1].buffer = 0;
    bufferViews[1].byteOffset = verticesOffset;
    bufferViews[1].byteLength = verticesSize;
    bufferViews[1].byteStride = sizeof(Vertex);
    bufferViews[1].target = TINYGLTF_TARGET_ARRAY_BUFFER;

    model.bufferViews.clear();
    model.bufferViews.push_back(bufferViews[0]);
    model.bufferViews.push_back(bufferViews[1]);
  }

  tinygltf::Accessor indexAccessor{};
  indexAccessor.bufferView    = 0;
  indexAccessor.type          = TINYGLTF_TYPE_SCALAR;
  indexAccessor.componentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT;
  indexAccessor.normalized    = false;

  tinygltf::Accessor positionAccessor{};
  positionAccessor.bufferView    = 1;
  positionAccessor.type          = TINYGLTF_TYPE_VEC3;
  positionAccessor.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
  positionAccessor.normalized    = false;

  tinygltf::Accessor normalAccessor{};
  normalAccessor.bufferView    = 1;
  normalAccessor.type          = TINYGLTF_TYPE_VEC3;
  normalAccessor.componentType = TINYGLTF_COMPONENT_TYPE_BYTE;
  normalAccessor.normalized    = true;

  tinygltf::Accessor texcoordAccessor{};
  texcoordAccessor.bufferView    = 1;
  texcoordAccessor.type          = TINYGLTF_TYPE_VEC2;
  texcoordAccessor.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
  texcoordAccessor.normalized    = false;

  tinygltf::Accessor tangentAccessor{};
  tangentAccessor.bufferView    = 1;
  tangentAccessor.type          = TINYGLTF_TYPE_VEC4;
  tangentAccessor.componentType = TINYGLTF_COMPONENT_TYPE_BYTE;
  tangentAccessor.normalized    = true;

  std::vector<tinygltf::Accessor> newAccessors;

  for (uint32_t meshIndex = 0; meshIndex < model.meshes.size(); ++meshIndex)
  {
      auto& mesh = model.meshes[meshIndex];

      for (uint32_t primIndex = 0; primIndex < mesh.primitives.size(); ++primIndex)
      {
          auto& primitive = mesh.primitives[primIndex];

          const bool hasNormals  = primitive.attributes.count("NORMAL")     != 0;
          const bool hasTexcoord = primitive.attributes.count("TEXCOORD_0")  != 0;
          const bool hasTangents = primitive.attributes.count("TANGENT")     != 0;

          auto& relem = bakedModel.relems[bakedModel.meshes[meshIndex].firstRelem + primIndex];
          const uint32_t indexOffset  = relem.indexOffset;
          const uint32_t indexCount   = relem.indexCount;
          const uint32_t vertexOffset = relem.vertexOffset;

          uint32_t maxIndex = 0;
          glm::vec3 minValues = glm::vec3(bakedModel.vertices[vertexOffset].positionAndNormal);
          glm::vec3 maxValues = minValues;

          for (uint32_t i = 0; i < indexCount; ++i)
          {
              const uint32_t index = bakedModel.indices[indexOffset + i];
              maxIndex = std::max(maxIndex, index);

              const glm::vec3 pos = glm::vec3(bakedModel.vertices[vertexOffset + index].positionAndNormal);
              minValues = glm::min(minValues, pos);
              maxValues = glm::max(maxValues, pos);
          }

          {
              auto accessor = indexAccessor;
              accessor.name       = "baked_indices_accessor";
              accessor.byteOffset = indexOffset * sizeof(uint32_t);
              accessor.count      = indexCount;

              primitive.indices = static_cast<int>(newAccessors.size());
              newAccessors.push_back(std::move(accessor));
          }

          primitive.attributes.clear();

          {
              auto accessor = positionAccessor;
              accessor.name       = "baked_position_accessor";
              accessor.byteOffset = vertexOffset * sizeof(Vertex);
              accessor.count      = maxIndex + 1;
              accessor.minValues  = {minValues.x, minValues.y, minValues.z};
              accessor.maxValues  = {maxValues.x, maxValues.y, maxValues.z};

              primitive.attributes["POSITION"] = static_cast<int>(newAccessors.size());
              newAccessors.push_back(std::move(accessor));
          }

          if (hasNormals)
          {
              auto accessor = normalAccessor;
              accessor.name       = "baked_normal_accessor";
              accessor.byteOffset = vertexOffset * sizeof(Vertex) + 3 * sizeof(float);
              accessor.count      = maxIndex + 1;

              primitive.attributes["NORMAL"] = static_cast<int>(newAccessors.size());
              newAccessors.push_back(std::move(accessor));
          }

          if (hasTexcoord)
          {
              auto accessor = texcoordAccessor;
              accessor.name       = "baked_texcoord_accessor";
              accessor.byteOffset = vertexOffset * sizeof(Vertex) + 4 * sizeof(float);
              accessor.count      = maxIndex + 1;

              primitive.attributes["TEXCOORD_0"] = static_cast<int>(newAccessors.size());
              newAccessors.push_back(std::move(accessor));
          }

          if (hasTangents)
          {
              auto accessor = tangentAccessor;
              accessor.name       = "baked_tangent_accessor";
              accessor.byteOffset = vertexOffset * sizeof(Vertex) + 6 * sizeof(float);
              accessor.count      = maxIndex + 1;

              primitive.attributes["TANGENT"] = static_cast<int>(newAccessors.size());
              newAccessors.push_back(std::move(accessor));
          }
      }
  }

  model.accessors = std::move(newAccessors);
  saveModel(model, bakedModelPath);


  return 0;
}
