#pragma once

#include <webgpu/webgpu_cpp.h>
#include <vector>
#include "cloud/CloudPoint.h"

namespace gfx {

class Renderer {
public:
  Renderer();
  void Initialize(const wgpu::Device& device, const wgpu::ShaderModule& shaderModule, wgpu::TextureFormat format, const std::vector<cloud::CloudPoint>& points, size_t uniformBufferSize);
  void UpdateUniforms(const void* data, size_t size);
  void UpdateIndexBuffer(const std::vector<uint32_t>& indices);
  void Render(wgpu::RenderPassEncoder& pass);
  uint32_t GetPointCount() const { return pointCount_; }
private:
  wgpu::Device device_;
  wgpu::RenderPipeline pipeline_;
  wgpu::Buffer pointBuffer_;
  wgpu::Buffer uniformBuffer_;
  wgpu::Buffer indexBuffer_;
  wgpu::BindGroup bindGroup_;
  uint32_t pointCount_ = 0;
};

} // namespace gfx
