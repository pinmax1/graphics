
add_executable(aa_renderer
  main.cpp
  App.cpp
  Renderer.cpp
  WorldRenderer.cpp
)

target_link_libraries(aa_renderer
  PRIVATE glfw etna glm::glm wsi baker gui scene render_utils)

target_add_shaders(aa_renderer
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
  shaders/fxaa.vert
  shaders/fxaa.frag
)
