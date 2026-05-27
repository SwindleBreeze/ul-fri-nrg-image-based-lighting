#include "mesh/CubeMesh.h"

namespace mesh {

MeshData CreateCubeMesh() {
  MeshData mesh;

  const glm::vec3 positions[8] = {
    { -1.0f, -1.0f, -1.0f },
    {  1.0f, -1.0f, -1.0f },
    {  1.0f,  1.0f, -1.0f },
    { -1.0f,  1.0f, -1.0f },
    { -1.0f, -1.0f,  1.0f },
    {  1.0f, -1.0f,  1.0f },
    {  1.0f,  1.0f,  1.0f },
    { -1.0f,  1.0f,  1.0f },
  };

  const uint32_t indices[36] = {
    0, 1, 2, 0, 2, 3,
    4, 6, 5, 4, 7, 6,
    0, 4, 5, 0, 5, 1,
    2, 6, 7, 2, 7, 3,
    0, 3, 7, 0, 7, 4,
    1, 5, 6, 1, 6, 2,
  };

  for (uint32_t i = 0; i < 36; ++i) {
    const glm::vec3& position = positions[indices[i]];
    const glm::vec3 normal = glm::normalize(position);
    mesh.vertices.push_back({ position, normal, glm::vec2(0.0f), glm::vec4(1.0f, 0.0f, 0.0f, 1.0f) });
    mesh.indices.push_back(i);
  }

  return mesh;
}

} // namespace mesh
