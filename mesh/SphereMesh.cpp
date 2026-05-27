#include "mesh/SphereMesh.h"

#include <cmath>

namespace mesh {

MeshData CreateSphereMesh(uint32_t segments, uint32_t rings) {
  MeshData mesh;

  constexpr float kPi = 3.14159265358979323846f;

  const float invSegments = 1.0f / static_cast<float>(segments);
  const float invRings = 1.0f / static_cast<float>(rings);

  for (uint32_t y = 0; y <= rings; ++y) {
    const float v = static_cast<float>(y) * invRings;
    const float theta = v * kPi;

    for (uint32_t x = 0; x <= segments; ++x) {
      const float u = static_cast<float>(x) * invSegments;
      const float phi = u * (kPi * 2.0f);

      const float sinTheta = std::sin(theta);
      const float cosTheta = std::cos(theta);
      const float sinPhi = std::sin(phi);
      const float cosPhi = std::cos(phi);

      glm::vec3 position(sinTheta * cosPhi, cosTheta, sinTheta * sinPhi);
      glm::vec3 normal = glm::normalize(position);
      glm::vec2 uv(u, 1.0f - v);
      const glm::vec3 tangent = glm::normalize(glm::vec3(-sinPhi, 0.0f, cosPhi));

      mesh.vertices.push_back({ position, normal, uv, glm::vec4(tangent, 1.0f) });
    }
  }

  const uint32_t stride = segments + 1;
  for (uint32_t y = 0; y < rings; ++y) {
    for (uint32_t x = 0; x < segments; ++x) {
      const uint32_t i0 = y * stride + x;
      const uint32_t i1 = i0 + 1;
      const uint32_t i2 = i0 + stride;
      const uint32_t i3 = i2 + 1;

      mesh.indices.push_back(i0);
      mesh.indices.push_back(i2);
      mesh.indices.push_back(i1);

      mesh.indices.push_back(i1);
      mesh.indices.push_back(i2);
      mesh.indices.push_back(i3);
    }
  }

  return mesh;
}

} // namespace mesh
