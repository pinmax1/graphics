
add_executable(terrain_baker
  main.cpp
)

target_link_libraries(terrain_baker
  PRIVATE tinygltf glm::glm etna)
