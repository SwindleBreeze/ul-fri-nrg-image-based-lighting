#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace mesh {

struct Vertex {
  glm::vec3 position;
  glm::vec3 normal;
  glm::vec2 uv;
  glm::vec4 tangent; // xyz + handedness in w
};

struct MeshData {
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
};

// Create a UV sphere mesh with the given segment and ring counts.
MeshData CreateSphereMesh(uint32_t segments, uint32_t rings);

} // namespace mesh
