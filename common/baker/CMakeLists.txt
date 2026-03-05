add_library(baker Baker.cpp)

target_link_libraries(baker
  PRIVATE tinygltf glm::glm etna scene)
