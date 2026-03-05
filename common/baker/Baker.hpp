#pragma once

#include <filesystem>
#include <glm/glm.hpp>
#include <spdlog/spdlog.h>
#include <optional>
#include <scene/SceneManager.hpp>
#include "tiny_gltf.h"

struct ProcessedInstances
{
  std::vector<glm::mat4x4> matrices;
  std::vector<std::uint32_t> meshes;
};

struct Vertex
{
  glm::vec4 positionAndNormal;
  glm::vec4 texCoordAndTangentAndPadding;
};

struct ProcessedMeshes
{
  std::vector<Vertex> vertices;
  std::vector<std::uint32_t> indices;
  std::vector<RenderElement> relems;
  std::vector<Mesh> meshes;
};

std::uint32_t encode_normal(glm::vec3 normal);



std::optional<tinygltf::Model> loadModel(std::filesystem::path path);

int saveModel(tinygltf::Model& model, std::filesystem::path& path);


ProcessedMeshes processMeshes(const tinygltf::Model& model);



int bakeModel(std::filesystem::path modelPath);
