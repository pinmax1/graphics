
add_executable(terrain2_renderer
  main.cpp
  App.cpp
  Renderer.cpp
  WorldRenderer.cpp
)

target_link_libraries(terrain2_renderer
  PRIVATE glfw etna glm::glm wsi gui scene render_utils)

target_add_shaders(terrain2_renderer
  shaders/static_mesh.frag
  shaders/static_mesh.vert
  shaders/terrain.vert
  shaders/terrain.tesc
  shaders/terrain.tese
  shaders/terrain.frag
  shaders/grass_placement.comp
  shaders/grass.vert
  shaders/grass.frag
  shaders/clipmap_update.vert
  shaders/clipmap_update.frag
)
