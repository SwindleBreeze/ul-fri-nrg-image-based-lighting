#pragma once

#include <string>

#include <glm/vec3.hpp>
#include <webgpu/webgpu_cpp.h>

namespace scene {

struct Material {
  glm::vec3 albedo{0.8f, 0.8f, 0.8f};
  float metallic = 0.0f;
  float roughness = 0.5f;
  float ao = 1.0f;
  bool useAlbedoMap = false;
  bool useMetallicRoughnessMap = false;
  bool useNormalMap = false;
  wgpu::TextureView albedoView;
  wgpu::TextureView metallicRoughnessView;
  wgpu::TextureView normalView;
};

} // namespace scene
