#pragma once

#include <string>
#include <vector>

#include <webgpu/webgpu_cpp.h>

#include "mesh/SphereMesh.h"
#include "scene/DrawObject.h"

namespace io {

struct GltfLoadResult {
  std::vector<scene::DrawObject> objects;
  std::vector<wgpu::Texture> ownedTextures;
  bool success = false;
};

GltfLoadResult LoadGltfScene(const wgpu::Device& device,
                               const wgpu::Queue& queue,
                               const std::string& path,
                               wgpu::TextureView defaultAlbedo,
                               wgpu::TextureView defaultMr,
                               wgpu::TextureView defaultNormal);

} // namespace io
