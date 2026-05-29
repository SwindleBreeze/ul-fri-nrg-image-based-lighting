#include "gfx/PbrRenderer.h"

#include "app/BakePipeline.h"
#include "gfx/UiLayer.h"

#include <cstring>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <iostream>

#include "mesh/CubeMesh.h"
#include "utils/WgslUtils.h"

namespace gfx {

namespace {

static void CopyMat4(const glm::mat4& src, float* dst) {
  std::memcpy(dst, &src[0][0], sizeof(float) * 16);
}

static uint32_t PackMaterialFlags(const scene::Material& material) {
  uint32_t flags = 0;
  if (material.useAlbedoMap) flags |= 1u;
  if (material.useMetallicRoughnessMap) flags |= 2u;
  if (material.useNormalMap) flags |= 4u;
  return flags;
}

} // namespace

void PbrRenderer::CreateDefaultTextures(const wgpu::Instance& instance) {
  const uint8_t pixels[3][4] = {
    { 255, 255, 255, 255 },
    { 0, 255, 0, 255 },   // glTF MR: roughness=1, metallic=0 (if map is enabled)
    { 128, 128, 255, 255 },
  };

  wgpu::TextureDescriptor desc{};
  desc.size = { 1, 1, 1 };
  desc.format = wgpu::TextureFormat::RGBA8Unorm;
  desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;

  defaultAlbedo_ = device_.CreateTexture(&desc);
  defaultMr_ = device_.CreateTexture(&desc);
  defaultNormal_ = device_.CreateTexture(&desc);

  wgpu::Texture textures[3] = { defaultAlbedo_, defaultMr_, defaultNormal_ };

  wgpu::BufferDescriptor stagingDesc{};
  stagingDesc.size = 256 * 3;
  stagingDesc.usage = wgpu::BufferUsage::CopySrc;
  stagingDesc.mappedAtCreation = true;
  wgpu::Buffer staging = device_.CreateBuffer(&stagingDesc);
  uint8_t* mapped = static_cast<uint8_t*>(staging.GetMappedRange());
  for (uint32_t i = 0; i < 3; ++i) {
    std::memcpy(mapped + i * 256, pixels[i], 4);
  }
  staging.Unmap();

  wgpu::CommandEncoder encoder = device_.CreateCommandEncoder();

  for (uint32_t i = 0; i < 3; ++i) {

    wgpu::TexelCopyBufferInfo src{};
    src.buffer = staging;
    src.layout.offset = i * 256;
    src.layout.bytesPerRow = 256;
    src.layout.rowsPerImage = 1;

    wgpu::TexelCopyTextureInfo dst{};
    dst.texture = textures[i];
    dst.mipLevel = 0;
    dst.origin = { 0, 0, 0 };
    dst.aspect = wgpu::TextureAspect::All;

    wgpu::Extent3D extent{ 1, 1, 1 };
    encoder.CopyBufferToTexture(&src, &dst, &extent);
  }

  queue_.Submit(1, &encoder.Finish());
  app::WaitForQueue(instance, queue_);

  defaultAlbedoView_ = defaultAlbedo_.CreateView();
  defaultMrView_ = defaultMr_.CreateView();
  defaultNormalView_ = defaultNormal_.CreateView();
}

wgpu::BindGroup PbrRenderer::CreateMaterialBindGroup(const scene::Material& material,
                                                   const wgpu::Buffer& materialBuffer,
                                                   const wgpu::Buffer& modelBuffer) {
  wgpu::BindGroupEntry albedoEntry{};
  albedoEntry.binding = 0;
  albedoEntry.textureView =
    material.albedoView ? material.albedoView : defaultAlbedoView_;

  wgpu::BindGroupEntry mrEntry{};
  mrEntry.binding = 1;
  mrEntry.textureView =
    material.metallicRoughnessView ? material.metallicRoughnessView : defaultMrView_;

  wgpu::BindGroupEntry normalEntry{};
  normalEntry.binding = 2;
  normalEntry.textureView = material.normalView ? material.normalView : defaultNormalView_;

  wgpu::BindGroupEntry samplerEntry{};
  samplerEntry.binding = 3;
  samplerEntry.sampler = materialSampler_;

  wgpu::BindGroupEntry materialUniformEntry{};
  materialUniformEntry.binding = 4;
  materialUniformEntry.buffer = materialBuffer;
  materialUniformEntry.size = sizeof(MaterialUniforms);

  wgpu::BindGroupEntry modelUniformEntry{};
  modelUniformEntry.binding = 5;
  modelUniformEntry.buffer = modelBuffer;
  modelUniformEntry.size = sizeof(float) * 16;

  wgpu::BindGroupEntry entries[6] = { albedoEntry, mrEntry, normalEntry, samplerEntry,
                                      materialUniformEntry, modelUniformEntry };
  wgpu::BindGroupDescriptor desc{};
  desc.layout = materialBgl_;
  desc.entryCount = 6;
  desc.entries = entries;
  return device_.CreateBindGroup(&desc);
}

void PbrRenderer::UploadDefaultTextures(const wgpu::Instance& instance,
                                        const wgpu::Device& device,
                                        const wgpu::Queue& queue) {
  device_ = device;
  queue_ = queue;
  if (!defaultTexturesUploaded_) {
    CreateDefaultTextures(instance);
    defaultTexturesUploaded_ = true;
  }
}

void PbrRenderer::Initialize(const wgpu::Instance& instance,
                             const wgpu::Device& device,
                             const wgpu::Queue& queue,
                             wgpu::TextureFormat colorFormat,
                             const ibl::IblTextures& iblTextures) {
  ready_ = false;
  device_ = device;
  queue_ = queue;
  colorFormat_ = colorFormat;
  iblTextures_ = iblTextures;
  maxReflectionLod_ = iblTextures.maxReflectionLod;

  if (!iblTextures_.envCubemapView || !iblTextures_.irradianceView ||
      !iblTextures_.prefilterView || !iblTextures_.brdfLutView ||
      !iblTextures_.linearClampSampler) {
    std::cerr << "PbrRenderer: IBL textures are incomplete after bake.\n";
    return;
  }

  if (!defaultTexturesUploaded_) {
    CreateDefaultTextures(instance);
    defaultTexturesUploaded_ = true;
  }

  wgpu::BufferDescriptor cameraDesc{};
  cameraDesc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
  cameraDesc.size = sizeof(CameraUniforms);
  cameraBuffer_ = device_.CreateBuffer(&cameraDesc);

  wgpu::BufferDescriptor skyCameraDesc{};
  skyCameraDesc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
  skyCameraDesc.size = sizeof(CameraUniforms);
  skyCameraBuffer_ = device_.CreateBuffer(&skyCameraDesc);

  wgpu::BufferDescriptor iblParamsDesc{};
  iblParamsDesc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
  iblParamsDesc.size = sizeof(IblParamsUniform);
  iblParamsBuffer_ = device_.CreateBuffer(&iblParamsDesc);

  wgpu::BufferDescriptor skyParamsDesc{};
  skyParamsDesc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
  skyParamsDesc.size = sizeof(SkyParamsUniform);
  skyParamsBuffer_ = device_.CreateBuffer(&skyParamsDesc);

  wgpu::BindGroupLayoutEntry camEntries[2] = {};
  camEntries[0].binding = 0;
  camEntries[0].visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
  camEntries[0].buffer.type = wgpu::BufferBindingType::Uniform;
  camEntries[1].binding = 1;
  camEntries[1].visibility = wgpu::ShaderStage::Fragment;
  camEntries[1].buffer.type = wgpu::BufferBindingType::Uniform;

  wgpu::BindGroupLayoutDescriptor camBglDesc{};
  camBglDesc.entryCount = 2;
  camBglDesc.entries = camEntries;
  cameraBgl_ = device_.CreateBindGroupLayout(&camBglDesc);

  wgpu::BindGroupLayoutEntry iblEntries[5] = {};
  iblEntries[0].binding = 0;
  iblEntries[0].visibility = wgpu::ShaderStage::Fragment;
  iblEntries[0].texture.sampleType = wgpu::TextureSampleType::Float;
  iblEntries[0].texture.viewDimension = wgpu::TextureViewDimension::Cube;
  iblEntries[1].binding = 1;
  iblEntries[1].visibility = wgpu::ShaderStage::Fragment;
  iblEntries[1].texture.sampleType = wgpu::TextureSampleType::Float;
  iblEntries[1].texture.viewDimension = wgpu::TextureViewDimension::Cube;
  iblEntries[2].binding = 2;
  iblEntries[2].visibility = wgpu::ShaderStage::Fragment;
  iblEntries[2].texture.sampleType = wgpu::TextureSampleType::UnfilterableFloat;
  iblEntries[2].texture.viewDimension = wgpu::TextureViewDimension::e2D;
  iblEntries[3].binding = 3;
  iblEntries[3].visibility = wgpu::ShaderStage::Fragment;
  iblEntries[3].sampler.type = wgpu::SamplerBindingType::Filtering;
  iblEntries[4].binding = 4;
  iblEntries[4].visibility = wgpu::ShaderStage::Fragment;
  iblEntries[4].sampler.type = wgpu::SamplerBindingType::NonFiltering;

  wgpu::BindGroupLayoutDescriptor iblBglDesc{};
  iblBglDesc.entryCount = 5;
  iblBglDesc.entries = iblEntries;
  iblBgl_ = device_.CreateBindGroupLayout(&iblBglDesc);

  wgpu::BindGroupLayoutEntry matEntries[6] = {};
  for (uint32_t i = 0; i < 3; ++i) {
    matEntries[i].binding = i;
    matEntries[i].visibility = wgpu::ShaderStage::Fragment;
    matEntries[i].texture.sampleType = wgpu::TextureSampleType::Float;
    matEntries[i].texture.viewDimension = wgpu::TextureViewDimension::e2D;
  }
  matEntries[3].binding = 3;
  matEntries[3].visibility = wgpu::ShaderStage::Fragment;
  matEntries[3].sampler.type = wgpu::SamplerBindingType::Filtering;
  matEntries[4].binding = 4;
  matEntries[4].visibility = wgpu::ShaderStage::Fragment;
  matEntries[4].buffer.type = wgpu::BufferBindingType::Uniform;
  matEntries[5].binding = 5;
  matEntries[5].visibility = wgpu::ShaderStage::Vertex;
  matEntries[5].buffer.type = wgpu::BufferBindingType::Uniform;

  wgpu::BindGroupLayoutDescriptor matBglDesc{};
  matBglDesc.entryCount = 6;
  matBglDesc.entries = matEntries;
  materialBgl_ = device_.CreateBindGroupLayout(&matBglDesc);

  wgpu::SamplerDescriptor matSamplerDesc{};
  matSamplerDesc.addressModeU = wgpu::AddressMode::Repeat;
  matSamplerDesc.addressModeV = wgpu::AddressMode::Repeat;
  matSamplerDesc.magFilter = wgpu::FilterMode::Linear;
  matSamplerDesc.minFilter = wgpu::FilterMode::Linear;
  materialSampler_ = device_.CreateSampler(&matSamplerDesc);

  wgpu::SamplerDescriptor brdfSamplerDesc{};
  brdfSamplerDesc.addressModeU = wgpu::AddressMode::ClampToEdge;
  brdfSamplerDesc.addressModeV = wgpu::AddressMode::ClampToEdge;
  brdfSamplerDesc.minFilter = wgpu::FilterMode::Nearest;
  brdfSamplerDesc.magFilter = wgpu::FilterMode::Nearest;
  brdfSampler_ = device_.CreateSampler(&brdfSamplerDesc);

  UpdateIblTextures(iblTextures);

  wgpu::BindGroupEntry camBgEntries[2] = {};
  camBgEntries[0].binding = 0;
  camBgEntries[0].buffer = cameraBuffer_;
  camBgEntries[0].size = sizeof(CameraUniforms);
  camBgEntries[1].binding = 1;
  camBgEntries[1].buffer = iblParamsBuffer_;
  camBgEntries[1].size = sizeof(IblParamsUniform);

  wgpu::BindGroupDescriptor camBgDesc{};
  camBgDesc.layout = cameraBgl_;
  camBgDesc.entryCount = 2;
  camBgDesc.entries = camBgEntries;
  cameraBindGroup_ = device_.CreateBindGroup(&camBgDesc);

  mesh::MeshData skyMesh = mesh::CreateCubeMesh();
  skyboxIndexCount_ = static_cast<uint32_t>(skyMesh.indices.size());

  wgpu::BufferDescriptor skyVbDesc{};
  skyVbDesc.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;
  skyVbDesc.size = skyMesh.vertices.size() * sizeof(mesh::Vertex);
  skyboxVertexBuffer_ = device_.CreateBuffer(&skyVbDesc);
  queue_.WriteBuffer(skyboxVertexBuffer_, 0, skyMesh.vertices.data(), skyVbDesc.size);

  wgpu::BufferDescriptor skyIbDesc{};
  skyIbDesc.usage = wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst;
  skyIbDesc.size = skyMesh.indices.size() * sizeof(uint32_t);
  skyboxIndexBuffer_ = device_.CreateBuffer(&skyIbDesc);
  queue_.WriteBuffer(skyboxIndexBuffer_, 0, skyMesh.indices.data(), skyIbDesc.size);

  wgpu::BindGroupLayoutEntry skyEntries[4] = {};
  skyEntries[0].binding = 0;
  skyEntries[0].visibility = wgpu::ShaderStage::Vertex;
  skyEntries[0].buffer.type = wgpu::BufferBindingType::Uniform;
  skyEntries[1].binding = 1;
  skyEntries[1].visibility = wgpu::ShaderStage::Fragment;
  skyEntries[1].texture.sampleType = wgpu::TextureSampleType::Float;
  skyEntries[1].texture.viewDimension = wgpu::TextureViewDimension::Cube;
  skyEntries[2].binding = 2;
  skyEntries[2].visibility = wgpu::ShaderStage::Fragment;
  skyEntries[2].sampler.type = wgpu::SamplerBindingType::Filtering;
  skyEntries[3].binding = 3;
  skyEntries[3].visibility = wgpu::ShaderStage::Fragment;
  skyEntries[3].buffer.type = wgpu::BufferBindingType::Uniform;

  wgpu::BindGroupLayoutDescriptor skyBglDesc{};
  skyBglDesc.entryCount = 4;
  skyBglDesc.entries = skyEntries;
  skyBgl_ = device_.CreateBindGroupLayout(&skyBglDesc);

  wgpu::BindGroupEntry skyBgEntries[4] = {};
  skyBgEntries[0].binding = 0;
  skyBgEntries[0].buffer = skyCameraBuffer_;
  skyBgEntries[0].size = sizeof(CameraUniforms);
  skyBgEntries[1].binding = 1;
  skyBgEntries[1].textureView = iblTextures_.envCubemapView;
  skyBgEntries[2].binding = 2;
  skyBgEntries[2].sampler = iblTextures_.linearClampSampler;
  skyBgEntries[3].binding = 3;
  skyBgEntries[3].buffer = skyParamsBuffer_;
  skyBgEntries[3].size = sizeof(SkyParamsUniform);

  wgpu::BindGroupDescriptor skyBgDesc{};
  skyBgDesc.layout = skyBgl_;
  skyBgDesc.entryCount = 4;
  skyBgDesc.entries = skyBgEntries;
  skyboxBindGroup_ = device_.CreateBindGroup(&skyBgDesc);

  const std::string shaderCode = utils::LoadShaderFile("shaders/pbr.wgsl");
  wgpu::ShaderModule shaderModule = utils::CreateWgslModule(device_, shaderCode);
  if (!shaderModule) {
    std::cerr << "PbrRenderer: failed to create shader module." << "\n";
    return;
  }

  wgpu::VertexAttribute attributes[4] = {};
  attributes[0].shaderLocation = 0;
  attributes[0].format = wgpu::VertexFormat::Float32x3;
  attributes[0].offset = 0;
  attributes[1].shaderLocation = 1;
  attributes[1].format = wgpu::VertexFormat::Float32x3;
  attributes[1].offset = sizeof(float) * 3;
  attributes[2].shaderLocation = 2;
  attributes[2].format = wgpu::VertexFormat::Float32x2;
  attributes[2].offset = sizeof(float) * 6;
  attributes[3].shaderLocation = 3;
  attributes[3].format = wgpu::VertexFormat::Float32x4;
  attributes[3].offset = sizeof(float) * 8;

  wgpu::VertexBufferLayout vbLayout{};
  vbLayout.arrayStride = sizeof(mesh::Vertex);
  vbLayout.attributeCount = 4;
  vbLayout.attributes = attributes;

  wgpu::VertexState vertexState{};
  vertexState.module = shaderModule;
  vertexState.entryPoint = "vs_main";
  vertexState.bufferCount = 1;
  vertexState.buffers = &vbLayout;

  wgpu::ColorTargetState colorTarget{};
  colorTarget.format = colorFormat;

  wgpu::FragmentState fragmentState{};
  fragmentState.module = shaderModule;
  fragmentState.entryPoint = "fs_main";
  fragmentState.targetCount = 1;
  fragmentState.targets = &colorTarget;

  wgpu::DepthStencilState depthState{};
  depthState.format = wgpu::TextureFormat::Depth24Plus;
  depthState.depthWriteEnabled = true;
  depthState.depthCompare = wgpu::CompareFunction::Less;

  wgpu::BindGroupLayout bgls[3] = { cameraBgl_, iblBgl_, materialBgl_ };
  wgpu::PipelineLayoutDescriptor plDesc{};
  plDesc.bindGroupLayoutCount = 3;
  plDesc.bindGroupLayouts = bgls;
  wgpu::PipelineLayout pipelineLayout = device_.CreatePipelineLayout(&plDesc);

  wgpu::RenderPipelineDescriptor pipelineDesc{};
  pipelineDesc.layout = pipelineLayout;
  pipelineDesc.vertex = vertexState;
  pipelineDesc.fragment = &fragmentState;
  pipelineDesc.depthStencil = &depthState;
  pipeline_ = device_.CreateRenderPipeline(&pipelineDesc);
  if (!pipeline_) {
    std::cerr << "PbrRenderer: failed to create PBR render pipeline." << "\n";
    return;
  }

  const std::string skyShaderCode = utils::LoadShaderFile("shaders/skybox.wgsl");
  wgpu::ShaderModule skyShaderModule = utils::CreateWgslModule(device_, skyShaderCode);
  if (!skyShaderModule) {
    std::cerr << "PbrRenderer: failed to create skybox shader module." << "\n";
    return;
  }

  wgpu::VertexAttribute skyAttributes[1] = {};
  skyAttributes[0].shaderLocation = 0;
  skyAttributes[0].format = wgpu::VertexFormat::Float32x3;
  skyAttributes[0].offset = 0;

  wgpu::VertexBufferLayout skyVbLayout{};
  skyVbLayout.arrayStride = sizeof(mesh::Vertex);
  skyVbLayout.attributeCount = 1;
  skyVbLayout.attributes = skyAttributes;

  wgpu::VertexState skyVertexState{};
  skyVertexState.module = skyShaderModule;
  skyVertexState.entryPoint = "vs_main";
  skyVertexState.bufferCount = 1;
  skyVertexState.buffers = &skyVbLayout;

  wgpu::FragmentState skyFragmentState{};
  skyFragmentState.module = skyShaderModule;
  skyFragmentState.entryPoint = "fs_main";
  skyFragmentState.targetCount = 1;
  skyFragmentState.targets = &colorTarget;

  wgpu::DepthStencilState skyDepthState{};
  skyDepthState.format = wgpu::TextureFormat::Depth24Plus;
  skyDepthState.depthWriteEnabled = false;
  skyDepthState.depthCompare = wgpu::CompareFunction::LessEqual;

  wgpu::PipelineLayoutDescriptor skyPlDesc{};
  skyPlDesc.bindGroupLayoutCount = 1;
  skyPlDesc.bindGroupLayouts = &skyBgl_;
  wgpu::PipelineLayout skyPipelineLayout = device_.CreatePipelineLayout(&skyPlDesc);

  wgpu::RenderPipelineDescriptor skyPipelineDesc{};
  skyPipelineDesc.layout = skyPipelineLayout;
  skyPipelineDesc.vertex = skyVertexState;
  skyPipelineDesc.fragment = &skyFragmentState;
  skyPipelineDesc.depthStencil = &skyDepthState;
  wgpu::PrimitiveState skyPrimitive{};
  skyPrimitive.cullMode = wgpu::CullMode::Front;
  skyPrimitive.frontFace = wgpu::FrontFace::CCW;
  skyPipelineDesc.primitive = skyPrimitive;

  skyboxPipeline_ = device_.CreateRenderPipeline(&skyPipelineDesc);
  if (!skyboxPipeline_) {
    std::cerr << "PbrRenderer: failed to create skybox render pipeline." << "\n";
    return;
  }

  CreateDepthTexture();
  ready_ = true;
}

void PbrRenderer::UpdateIblTextures(const ibl::IblTextures& iblTextures) {
  iblTextures_ = iblTextures;
  maxReflectionLod_ = iblTextures.maxReflectionLod;

  wgpu::BindGroupEntry irrBind{};
  irrBind.binding = 0;
  irrBind.textureView = iblTextures.irradianceView;

  wgpu::BindGroupEntry preBind{};
  preBind.binding = 1;
  preBind.textureView = iblTextures.prefilterView;

  wgpu::BindGroupEntry lutBind{};
  lutBind.binding = 2;
  lutBind.textureView = iblTextures.brdfLutView;

  wgpu::BindGroupEntry sampBind{};
  sampBind.binding = 3;
  sampBind.sampler = iblTextures.linearClampSampler;

  wgpu::BindGroupEntry brdfSampBind{};
  brdfSampBind.binding = 4;
  brdfSampBind.sampler = brdfSampler_;

  wgpu::BindGroupEntry iblBgEntries[5] = { irrBind, preBind, lutBind, sampBind, brdfSampBind };
  wgpu::BindGroupDescriptor iblBgDesc{};
  iblBgDesc.layout = iblBgl_;
  iblBgDesc.entryCount = 5;
  iblBgDesc.entries = iblBgEntries;
  iblBindGroup_ = device_.CreateBindGroup(&iblBgDesc);

  if (!skyBgl_) {
    return;
  }

  wgpu::BindGroupEntry skyEnvBind{};
  skyEnvBind.binding = 1;
  skyEnvBind.textureView = iblTextures.envCubemapView;

  wgpu::BindGroupEntry skyBgEntries[4] = {};
  skyBgEntries[0].binding = 0;
  skyBgEntries[0].buffer = skyCameraBuffer_;
  skyBgEntries[0].size = sizeof(CameraUniforms);
  skyBgEntries[1] = skyEnvBind;
  skyBgEntries[2].binding = 2;
  skyBgEntries[2].sampler = iblTextures.linearClampSampler;
  skyBgEntries[3].binding = 3;
  skyBgEntries[3].buffer = skyParamsBuffer_;
  skyBgEntries[3].size = sizeof(SkyParamsUniform);

  wgpu::BindGroupDescriptor skyBgDesc{};
  skyBgDesc.layout = skyBgl_;
  skyBgDesc.entryCount = 4;
  skyBgDesc.entries = skyBgEntries;
  skyboxBindGroup_ = device_.CreateBindGroup(&skyBgDesc);
}

void PbrRenderer::SetScene(scene::Scene* scene) {
  scene_ = scene;
  gpuObjects_.clear();
  if (!scene_) {
    return;
  }

  wgpu::BufferDescriptor materialDesc{};
  materialDesc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
  materialDesc.size = sizeof(MaterialUniforms);

  wgpu::BufferDescriptor modelDesc{};
  modelDesc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
  modelDesc.size = sizeof(float) * 16;

  for (const scene::DrawObject& object : scene_->objects) {
    GpuObject gpu{};
    gpu.object = object;
    gpu.materialBuffer = device_.CreateBuffer(&materialDesc);
    gpu.modelBuffer = device_.CreateBuffer(&modelDesc);
    gpu.materialBindGroup =
      CreateMaterialBindGroup(object.material, gpu.materialBuffer, gpu.modelBuffer);
    gpuObjects_.push_back(gpu);
  }
}

void PbrRenderer::Resize(uint32_t width, uint32_t height) {
  width_ = width;
  height_ = height;
  CreateDepthTexture();
  CreateScreenshotColorTexture();
}

void PbrRenderer::CreateDepthTexture() {
  wgpu::TextureDescriptor depthDesc{};
  depthDesc.size = { width_, height_, 1 };
  depthDesc.format = wgpu::TextureFormat::Depth24Plus;
  depthDesc.usage = wgpu::TextureUsage::RenderAttachment;
  depthTexture_ = device_.CreateTexture(&depthDesc);
  depthView_ = depthTexture_.CreateView();
}

void PbrRenderer::CreateScreenshotColorTexture() {
  if (!device_ || width_ == 0 || height_ == 0) {
    return;
  }

  wgpu::TextureDescriptor desc{};
  desc.size = { width_, height_, 1 };
  desc.format = colorFormat_;
  desc.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc;
  screenshotColor_ = device_.CreateTexture(&desc);
  screenshotColorView_ = screenshotColor_.CreateView();
}

void PbrRenderer::Render(const wgpu::TextureView& colorView,
                         const glm::vec3& cameraPos,
                         const glm::mat4& view,
                         const glm::mat4& proj,
                         float envYawRadians,
                         UiLayer* uiLayer) {
  if (!pipeline_ || !skyboxPipeline_) {
    return;
  }

  const glm::mat4 viewProj = proj * view;

  glm::mat4 viewRotOnly = view;
  viewRotOnly[3] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
  const glm::mat4 skyViewProj = proj * viewRotOnly;

  CameraUniforms skyCamera{};
  CopyMat4(skyViewProj, skyCamera.viewProj);
  queue_.WriteBuffer(skyCameraBuffer_, 0, &skyCamera, sizeof(CameraUniforms));

  SkyParamsUniform skyParams{};
  skyParams.envYaw = envYawRadians;
  queue_.WriteBuffer(skyParamsBuffer_, 0, &skyParams, sizeof(SkyParamsUniform));

  CameraUniforms camera{};
  CopyMat4(viewProj, camera.viewProj);
  camera.cameraPos[0] = cameraPos.x;
  camera.cameraPos[1] = cameraPos.y;
  camera.cameraPos[2] = cameraPos.z;
  camera.cameraPos[3] = 1.0f;
  queue_.WriteBuffer(cameraBuffer_, 0, &camera, sizeof(CameraUniforms));

  IblParamsUniform iblParams{};
  iblParams.maxReflectionLod = maxReflectionLod_;
  iblParams.envYaw = envYawRadians;
  queue_.WriteBuffer(iblParamsBuffer_, 0, &iblParams, sizeof(IblParamsUniform));

  for (size_t i = 0; i < gpuObjects_.size(); ++i) {
    const GpuObject& gpu = gpuObjects_[i];
    const scene::Material& mat =
      scene_ ? scene_->objects[i].material : gpu.object.material;
    MaterialUniforms materialUniforms{};
    materialUniforms.albedoMetallic[0] = mat.albedo.r;
    materialUniforms.albedoMetallic[1] = mat.albedo.g;
    materialUniforms.albedoMetallic[2] = mat.albedo.b;
    materialUniforms.albedoMetallic[3] = mat.metallic;
    materialUniforms.roughnessAoFlags[0] = mat.roughness;
    materialUniforms.roughnessAoFlags[1] = mat.ao;
    materialUniforms.roughnessAoFlags[2] = static_cast<float>(PackMaterialFlags(mat));
    queue_.WriteBuffer(gpu.materialBuffer, 0, &materialUniforms, sizeof(MaterialUniforms));

    float modelMatrix[16];
    CopyMat4(gpu.object.model, modelMatrix);
    queue_.WriteBuffer(gpu.modelBuffer, 0, modelMatrix, sizeof(modelMatrix));
  }

  wgpu::CommandEncoder encoder = device_.CreateCommandEncoder();

  wgpu::RenderPassColorAttachment skyColorAttachment{};
  skyColorAttachment.view = colorView;
  skyColorAttachment.loadOp = wgpu::LoadOp::Clear;
  skyColorAttachment.storeOp = wgpu::StoreOp::Store;
  skyColorAttachment.clearValue = { 0.05, 0.05, 0.06, 1.0 };

  wgpu::RenderPassDepthStencilAttachment skyDepthAttachment{};
  skyDepthAttachment.view = depthView_;
  skyDepthAttachment.depthLoadOp = wgpu::LoadOp::Clear;
  skyDepthAttachment.depthStoreOp = wgpu::StoreOp::Store;
  skyDepthAttachment.depthClearValue = 1.0f;

  wgpu::RenderPassDescriptor skyPassDesc{};
  skyPassDesc.colorAttachmentCount = 1;
  skyPassDesc.colorAttachments = &skyColorAttachment;
  skyPassDesc.depthStencilAttachment = &skyDepthAttachment;

  wgpu::RenderPassEncoder skyPass = encoder.BeginRenderPass(&skyPassDesc);
  skyPass.SetPipeline(skyboxPipeline_);
  skyPass.SetBindGroup(0, skyboxBindGroup_);
  skyPass.SetVertexBuffer(0, skyboxVertexBuffer_);
  skyPass.SetIndexBuffer(skyboxIndexBuffer_, wgpu::IndexFormat::Uint32);
  skyPass.DrawIndexed(skyboxIndexCount_, 1, 0, 0, 0);
  skyPass.End();

  wgpu::RenderPassColorAttachment objectColorAttachment{};
  objectColorAttachment.view = colorView;
  objectColorAttachment.loadOp = wgpu::LoadOp::Load;
  objectColorAttachment.storeOp = wgpu::StoreOp::Store;

  wgpu::RenderPassDepthStencilAttachment objectDepthAttachment{};
  objectDepthAttachment.view = depthView_;
  objectDepthAttachment.depthLoadOp = wgpu::LoadOp::Load;
  objectDepthAttachment.depthStoreOp = wgpu::StoreOp::Store;

  wgpu::RenderPassDescriptor objectPassDesc{};
  objectPassDesc.colorAttachmentCount = 1;
  objectPassDesc.colorAttachments = &objectColorAttachment;
  objectPassDesc.depthStencilAttachment = &objectDepthAttachment;

  wgpu::RenderPassEncoder objectPass = encoder.BeginRenderPass(&objectPassDesc);
  objectPass.SetPipeline(pipeline_);
  objectPass.SetBindGroup(0, cameraBindGroup_);
  objectPass.SetBindGroup(1, iblBindGroup_);

  auto drawObject = [&](const GpuObject& gpu) {
    objectPass.SetBindGroup(2, gpu.materialBindGroup);
    objectPass.SetVertexBuffer(0, gpu.object.vertexBuffer);
    objectPass.SetIndexBuffer(gpu.object.indexBuffer, wgpu::IndexFormat::Uint32);
    objectPass.DrawIndexed(gpu.object.indexCount, 1, 0, 0, 0);
  };

  for (const GpuObject& gpu : gpuObjects_) {
    if (gpu.object.name == "ground") {
      drawObject(gpu);
    }
  }
  for (const GpuObject& gpu : gpuObjects_) {
    if (gpu.object.name != "ground") {
      drawObject(gpu);
    }
  }

  objectPass.End();

  if (uiLayer) {
    wgpu::RenderPassColorAttachment uiColorAttachment{};
    uiColorAttachment.view = colorView;
    uiColorAttachment.loadOp = wgpu::LoadOp::Load;
    uiColorAttachment.storeOp = wgpu::StoreOp::Store;

    wgpu::RenderPassDescriptor uiPassDesc{};
    uiPassDesc.colorAttachmentCount = 1;
    uiPassDesc.colorAttachments = &uiColorAttachment;

    wgpu::RenderPassEncoder uiPass = encoder.BeginRenderPass(&uiPassDesc);
    uiLayer->RenderDrawData(uiPass);
    uiPass.End();
  }

  queue_.Submit(1, &encoder.Finish());
}

} // namespace gfx
