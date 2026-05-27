#pragma once

#include <string>
#include <vector>

#include <webgpu/webgpu_cpp.h>

#include "scene/DrawObject.h"

namespace scene {

struct Scene {
  std::vector<DrawObject> objects;
  std::vector<wgpu::Texture> ownedTextures;
};

Scene BuildDefaultScene(const wgpu::Device& device,
                        const wgpu::Queue& queue,
                        const std::string& gltfPath,
                        wgpu::TextureView defaultAlbedo,
                        wgpu::TextureView defaultMr,
                        wgpu::TextureView defaultNormal);

} // namespace scene
