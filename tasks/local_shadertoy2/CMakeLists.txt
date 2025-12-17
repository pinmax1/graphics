
add_executable(local_shadertoy2
  main.cpp
  App.cpp
)

target_link_libraries(local_shadertoy2
  PRIVATE glfw etna glm::glm wsi gui render_utils tinygltf)

target_add_shaders(local_shadertoy2
  shaders/toy.frag
  shaders/toy.vert
  shaders/texture.frag
  shaders/texture.vert
)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/textures
     DESTINATION ${CMAKE_BINARY_DIR}/tasks/local_shadertoy2)
