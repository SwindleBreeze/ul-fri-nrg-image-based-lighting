#pragma once

#include <cstdint>
#include <string>

#include <glm/mat4x4.hpp>
#include <webgpu/webgpu_cpp.h>

#include "scene/Material.h"

namespace scene {

struct DrawObject {
  std::string name;
  wgpu::Buffer vertexBuffer;
  wgpu::Buffer indexBuffer;
  uint32_t indexCount = 0;
  glm::mat4 model{1.0f};
  Material material;
};

} // namespace scene
