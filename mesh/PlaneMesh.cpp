#include "mesh/PlaneMesh.h"

namespace mesh {

MeshData CreatePlaneMesh(float halfExtent) {
  MeshData mesh;
  const float y = 0.0f;

  mesh.vertices = {
    { {-halfExtent, y, -halfExtent}, {0, 1, 0}, {0, 0}, {1, 0, 0, 1} },
    { { halfExtent, y, -halfExtent}, {0, 1, 0}, {1, 0}, {1, 0, 0, 1} },
    { { halfExtent, y,  halfExtent}, {0, 1, 0}, {1, 1}, {1, 0, 0, 1} },
    { {-halfExtent, y,  halfExtent}, {0, 1, 0}, {0, 1}, {1, 0, 0, 1} },
  };

  // CCW when viewed from +Y (camera above): normal points +Y.
  mesh.indices = { 0, 1, 2, 0, 2, 3 };
  return mesh;
}

} // namespace mesh
