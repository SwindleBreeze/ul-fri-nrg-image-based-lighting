#include "gfx/Renderer.h"
#include <iostream>

namespace gfx {

Renderer::Renderer() {}

void Renderer::Initialize(const wgpu::Device& device, const wgpu::ShaderModule& shaderModule, wgpu::TextureFormat format, const std::vector<cloud::CloudPoint>& points, size_t uniformBufferSize) {
  device_ = device;
  pointCount_ = static_cast<uint32_t>(points.size());

  uint64_t pointsSize = static_cast<uint64_t>(sizeof(cloud::CloudPoint) * points.size());

  wgpu::BufferDescriptor pointBufferDesc;
  pointBufferDesc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
  pointBufferDesc.size = pointsSize;
  pointBufferDesc.mappedAtCreation = false;
  pointBuffer_ = device_.CreateBuffer(&pointBufferDesc);

  wgpu::BufferDescriptor indexBufferDesc;
  indexBufferDesc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
  indexBufferDesc.size = sizeof(uint32_t) * points.size();
  indexBufferDesc.mappedAtCreation = false;
  indexBuffer_ = device_.CreateBuffer(&indexBufferDesc);

  // upload point data once
  device_.GetQueue().WriteBuffer(pointBuffer_, 0, points.data(), pointsSize);

  wgpu::BufferDescriptor uniformBufferDesc;
  uniformBufferDesc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
  uniformBufferDesc.size = static_cast<uint64_t>(uniformBufferSize);
  uniformBufferDesc.mappedAtCreation = false;
  uniformBuffer_ = device_.CreateBuffer(&uniformBufferDesc);

  // create bind group layout
  wgpu::BindGroupLayoutEntry bglEntry{};
  bglEntry.binding = 0;
  bglEntry.visibility = wgpu::ShaderStage::Vertex;
  bglEntry.buffer.type = wgpu::BufferBindingType::Uniform;

  wgpu::BindGroupLayoutEntry bglEntry2{};
  bglEntry2.binding = 1;
  bglEntry2.visibility = wgpu::ShaderStage::Vertex;
  bglEntry2.buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;

  wgpu::BindGroupLayoutEntry bglEntry3{};
  bglEntry3.binding = 2;
  bglEntry3.visibility = wgpu::ShaderStage::Vertex;
  bglEntry3.buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;

  wgpu::BindGroupLayoutDescriptor bindGroupLayoutDesc{};
  bindGroupLayoutDesc.entryCount = 3;
  wgpu::BindGroupLayoutEntry bglEntries[3] = { bglEntry, bglEntry2, bglEntry3 };
  bindGroupLayoutDesc.entries = bglEntries;
  wgpu::BindGroupLayout bindGroupLayout = device_.CreateBindGroupLayout(&bindGroupLayoutDesc);

  wgpu::BindGroupEntry bindGroupEntry{};
  bindGroupEntry.binding = 0;
  bindGroupEntry.buffer = uniformBuffer_;
  bindGroupEntry.offset = 0;
  bindGroupEntry.size = uniformBufferDesc.size;

  wgpu::BindGroupEntry bindGroupEntry2{};
  bindGroupEntry2.binding = 1;
  bindGroupEntry2.buffer = pointBuffer_;
  bindGroupEntry2.offset = 0;
  bindGroupEntry2.size = pointsSize;

  wgpu::BindGroupEntry bindGroupEntry3{};
  bindGroupEntry3.binding = 2;
  bindGroupEntry3.buffer = indexBuffer_;
  bindGroupEntry3.offset = 0;
  bindGroupEntry3.size = sizeof(uint32_t) * pointCount_;

  wgpu::BindGroupDescriptor bindGroupDesc{};
  bindGroupDesc.layout = bindGroupLayout;
  bindGroupDesc.entryCount = 3;
  wgpu::BindGroupEntry bgEntries[3] = { bindGroupEntry, bindGroupEntry2, bindGroupEntry3 };
  bindGroupDesc.entries = bgEntries;
  bindGroup_ = device_.CreateBindGroup(&bindGroupDesc);

  // create pipeline (vertex/fragment are in same module)
  wgpu::PipelineLayoutDescriptor pipelineLayoutDesc{};
  pipelineLayoutDesc.bindGroupLayoutCount = 1;
  pipelineLayoutDesc.bindGroupLayouts = &bindGroupLayout;
  wgpu::PipelineLayout pipelineLayout = device_.CreatePipelineLayout(&pipelineLayoutDesc);

  wgpu::BlendState blendState{};
  blendState.color.srcFactor = wgpu::BlendFactor::One;
  blendState.color.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
  blendState.color.operation = wgpu::BlendOperation::Add;
  blendState.alpha.srcFactor = wgpu::BlendFactor::One;
  blendState.alpha.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
  blendState.alpha.operation = wgpu::BlendOperation::Add;

  wgpu::ColorTargetState colorTarget{};
  colorTarget.format = format;
  colorTarget.blend = &blendState;
  colorTarget.writeMask = wgpu::ColorWriteMask::All;

  wgpu::VertexState vertexState{};
  vertexState.module = shaderModule;
  vertexState.entryPoint = "vs_main";
  vertexState.bufferCount = 0;
  vertexState.buffers = nullptr;

  wgpu::FragmentState fragmentState{};
  fragmentState.module = shaderModule;
  fragmentState.entryPoint = "fs_main";
  fragmentState.targetCount = 1;
  fragmentState.targets = &colorTarget;

  wgpu::PrimitiveState primitiveState{};
  primitiveState.topology = wgpu::PrimitiveTopology::TriangleList;

  wgpu::RenderPipelineDescriptor pipelineDesc{};
  pipelineDesc.layout = pipelineLayout;
  pipelineDesc.primitive = primitiveState;
  pipelineDesc.vertex = vertexState;
  pipelineDesc.fragment = &fragmentState;
  pipeline_ = device_.CreateRenderPipeline(&pipelineDesc);
}

void Renderer::UpdateUniforms(const void* data, size_t size) {
  if (!uniformBuffer_.Get()) return;
  device_.GetQueue().WriteBuffer(uniformBuffer_, 0, data, static_cast<uint64_t>(size));
}

void Renderer::UpdateIndexBuffer(const std::vector<uint32_t>& indices) {
  if (!indexBuffer_.Get()) return;
  device_.GetQueue().WriteBuffer(indexBuffer_, 0, indices.data(), sizeof(uint32_t) * indices.size());
}

void Renderer::Render(wgpu::RenderPassEncoder& pass) {
  pass.SetPipeline(pipeline_);
  pass.SetBindGroup(0, bindGroup_);
  if (pointCount_ > 0) {
    pass.Draw(6, pointCount_, 0, 0);
  }
}

} // namespace gfx
