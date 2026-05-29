#include "ibl/IblBaker.h"

#include <array>
#include <chrono>
#include <iostream>
#include <string>

#include "utils/QueueSync.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "utils/WgslUtils.h"

namespace ibl {

namespace {

static glm::mat4 MakeCaptureProjection() {
  glm::mat4 projection = glm::perspectiveRH_ZO(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
  projection[1][1] *= -1.0f; // WebGPU/D3D clip-space Y points down.
  return projection;
}

struct CaptureUniforms {
  glm::mat4 viewProj;
};

// Each face gets its own submit so queue.WriteBuffer is visible before that face draws.
static void DrawCubemapFace(const wgpu::Device& device,
                            const wgpu::Queue& queue,
                            wgpu::RenderPipeline pipeline,
                            wgpu::BindGroup bindGroup,
                            wgpu::Buffer captureUbo,
                            const CaptureUniforms& uniforms,
                            const wgpu::Texture& target,
                            uint32_t face,
                            uint32_t mipLevel,
                            float viewportSize = 0.0f) {
  queue.WriteBuffer(captureUbo, 0, &uniforms, sizeof(CaptureUniforms));

  wgpu::TextureViewDescriptor faceViewDesc{};
  faceViewDesc.dimension = wgpu::TextureViewDimension::e2D;
  faceViewDesc.baseArrayLayer = face;
  faceViewDesc.arrayLayerCount = 1;
  faceViewDesc.baseMipLevel = mipLevel;
  faceViewDesc.mipLevelCount = 1;
  wgpu::TextureView faceView = target.CreateView(&faceViewDesc);

  wgpu::CommandEncoder encoder = device.CreateCommandEncoder();

  wgpu::RenderPassColorAttachment colorAttachment{};
  colorAttachment.view = faceView;
  colorAttachment.loadOp = wgpu::LoadOp::Clear;
  colorAttachment.storeOp = wgpu::StoreOp::Store;
  colorAttachment.clearValue = { 0.0, 0.0, 0.0, 1.0 };

  wgpu::RenderPassDescriptor passDesc{};
  passDesc.colorAttachmentCount = 1;
  passDesc.colorAttachments = &colorAttachment;

  wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&passDesc);
  pass.SetPipeline(pipeline);
  pass.SetBindGroup(0, bindGroup);
  if (viewportSize > 0.0f) {
    pass.SetViewport(0.0f, 0.0f, viewportSize, viewportSize, 0.0f, 1.0f);
  }
  pass.Draw(36, 1, 0, 0);
  pass.End();

  queue.Submit(1, &encoder.Finish());
}

// Build the 6 view matrices used to capture cubemap faces.
static std::array<glm::mat4, 6> BuildCaptureViews() {
  const glm::vec3 origin(0.0f, 0.0f, 0.0f);

  return {
    glm::lookAt(origin, origin + glm::vec3( 1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)), // +X
    glm::lookAt(origin, origin + glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)), // -X
    glm::lookAt(origin, origin + glm::vec3( 0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  1.0f)), // +Y
    glm::lookAt(origin, origin + glm::vec3( 0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)), // -Y
    glm::lookAt(origin, origin + glm::vec3( 0.0f,  0.0f,  1.0f), glm::vec3(0.0f, -1.0f,  0.0f)), // +Z
    glm::lookAt(origin, origin + glm::vec3( 0.0f,  0.0f, -1.0f), glm::vec3(0.0f, -1.0f,  0.0f))  // -Z
  };
}

// Uniforms for the env cubemap mip downsample pass.
struct DownsampleUniforms {
  float sourceMip = 0.0f; // Mip level we sample from.
  float padding0 = 0.0f;  // Padding to keep 16-byte alignment.
  float padding1 = 0.0f;
  float padding2 = 0.0f;
};

struct IrradianceUniforms {
  float sampleDelta = 0.025f;
  float padding0 = 0.0f;
  float padding1 = 0.0f;
  float padding2 = 0.0f;
};

struct PrefilterUniforms {
  float roughness = 0.0f;
  float resolution = 512.0f;
  float sampleCount = 1024.0f;
  float padding0 = 0.0f;
};

struct BrdfUniforms {
  float sampleCount = 1024.0f;
  float padding0 = 0.0f;
  float padding1 = 0.0f;
  float padding2 = 0.0f;
};

double ElapsedMs(const std::chrono::steady_clock::time_point& start) {
  const auto end = std::chrono::steady_clock::now();
  return std::chrono::duration<double, std::milli>(end - start).count();
}

void SyncIfTiming(const wgpu::Instance* syncInstance,
                  const wgpu::Queue& queue,
                  IblBakeTimings* timingsOut) {
  if (timingsOut && syncInstance) {
    utils::WaitForQueue(*syncInstance, queue);
    syncInstance->ProcessEvents();
  }
}

// Generate mip levels for the environment cubemap by downsampling each face.
// This is the explicit replacement for generateMipmap (which doesn't exist in WebGPU).
static void GenerateEnvCubemapMips(const wgpu::Device& device,
                                   const wgpu::Queue& queue,
                                   const wgpu::Texture& envCubemap,
                                   const wgpu::TextureView& envCubeView,
                                   const wgpu::Sampler& sampler,
                                   const wgpu::Buffer& captureUbo,
                                   const glm::mat4& projection,
                                   const std::array<glm::mat4, 6>& views,
                                   const IblBakeSettings& settings) {
  (void)envCubeView;
  const uint32_t envFaceSize = settings.envFaceSize;
  const uint32_t envMipCount = EnvMipLevelCount(envFaceSize);
  // Load the downsample shader.
  const std::string downsampleCode = utils::LoadShaderFile("shaders/cubemap_mip_downsample.wgsl");
  wgpu::ShaderModule downsampleModule = utils::CreateWgslModule(device, downsampleCode);
  if (!downsampleModule) {
    return;
  }

  // Uniform buffer for the source mip value.
  wgpu::BufferDescriptor mipUboDesc{};
  mipUboDesc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
  mipUboDesc.size = sizeof(DownsampleUniforms);
  wgpu::Buffer mipUbo = device.CreateBuffer(&mipUboDesc);

  // Temporary cubemap used as the read source during downsampling.
  wgpu::TextureDescriptor tempDesc{};
  tempDesc.size = { envFaceSize, envFaceSize, 6 };
  tempDesc.mipLevelCount = envMipCount;
  tempDesc.sampleCount = 1;
  tempDesc.dimension = wgpu::TextureDimension::e2D;
  tempDesc.format = wgpu::TextureFormat::RGBA16Float;
  tempDesc.usage = wgpu::TextureUsage::TextureBinding
                 | wgpu::TextureUsage::CopySrc
                 | wgpu::TextureUsage::CopyDst;
  wgpu::Texture tempCubemap = device.CreateTexture(&tempDesc);

  wgpu::TextureViewDescriptor tempViewDesc{};
  tempViewDesc.dimension = wgpu::TextureViewDimension::Cube;
  tempViewDesc.baseArrayLayer = 0;
  tempViewDesc.arrayLayerCount = 6;
  tempViewDesc.baseMipLevel = 0;
  tempViewDesc.mipLevelCount = tempDesc.mipLevelCount;
  wgpu::TextureView tempCubeView = tempCubemap.CreateView(&tempViewDesc);

  // Bind group layout: viewProj + env cube + sampler + sourceMip.
  wgpu::BindGroupLayoutEntry viewEntry{};
  viewEntry.binding = 0;
  viewEntry.visibility = wgpu::ShaderStage::Vertex;
  viewEntry.buffer.type = wgpu::BufferBindingType::Uniform;

  wgpu::BindGroupLayoutEntry cubeEntry{};
  cubeEntry.binding = 1;
  cubeEntry.visibility = wgpu::ShaderStage::Fragment;
  cubeEntry.texture.sampleType = wgpu::TextureSampleType::Float;
  cubeEntry.texture.viewDimension = wgpu::TextureViewDimension::Cube;

  wgpu::BindGroupLayoutEntry samplerEntry{};
  samplerEntry.binding = 2;
  samplerEntry.visibility = wgpu::ShaderStage::Fragment;
  samplerEntry.sampler.type = wgpu::SamplerBindingType::Filtering;

  wgpu::BindGroupLayoutEntry mipEntry{};
  mipEntry.binding = 3;
  mipEntry.visibility = wgpu::ShaderStage::Fragment;
  mipEntry.buffer.type = wgpu::BufferBindingType::Uniform;

  wgpu::BindGroupLayoutEntry bglEntries[4] = { viewEntry, cubeEntry, samplerEntry, mipEntry };
  wgpu::BindGroupLayoutDescriptor bglDesc{};
  bglDesc.entryCount = 4;
  bglDesc.entries = bglEntries;
  wgpu::BindGroupLayout bgl = device.CreateBindGroupLayout(&bglDesc);

  // Bind group uses the temp cube view so we never sample from the render target.
  wgpu::BindGroupEntry viewBind{};
  viewBind.binding = 0;
  viewBind.buffer = captureUbo;
  viewBind.offset = 0;
  viewBind.size = sizeof(CaptureUniforms);

  wgpu::BindGroupEntry cubeBind{};
  cubeBind.binding = 1;
  cubeBind.textureView = tempCubeView;

  wgpu::BindGroupEntry samplerBind{};
  samplerBind.binding = 2;
  samplerBind.sampler = sampler;

  wgpu::BindGroupEntry mipBind{};
  mipBind.binding = 3;
  mipBind.buffer = mipUbo;
  mipBind.offset = 0;
  mipBind.size = sizeof(DownsampleUniforms);

  wgpu::BindGroupEntry bgEntries[4] = { viewBind, cubeBind, samplerBind, mipBind };
  wgpu::BindGroupDescriptor bgDesc{};
  bgDesc.layout = bgl;
  bgDesc.entryCount = 4;
  bgDesc.entries = bgEntries;
  wgpu::BindGroup bindGroup = device.CreateBindGroup(&bgDesc);

  // Pipeline layout and render pipeline.
  wgpu::PipelineLayoutDescriptor plDesc{};
  plDesc.bindGroupLayoutCount = 1;
  plDesc.bindGroupLayouts = &bgl;
  wgpu::PipelineLayout pipelineLayout = device.CreatePipelineLayout(&plDesc);

  wgpu::ColorTargetState colorTarget{};
  colorTarget.format = wgpu::TextureFormat::RGBA16Float;
  colorTarget.writeMask = wgpu::ColorWriteMask::All;

  wgpu::FragmentState fragmentState{};
  fragmentState.module = downsampleModule;
  fragmentState.entryPoint = "fs_main";
  fragmentState.targetCount = 1;
  fragmentState.targets = &colorTarget;

  wgpu::VertexState vertexState{};
  vertexState.module = downsampleModule;
  vertexState.entryPoint = "vs_main";
  vertexState.bufferCount = 0;

  wgpu::PrimitiveState primitive{};
  primitive.topology = wgpu::PrimitiveTopology::TriangleList;
  primitive.cullMode = wgpu::CullMode::None;

  wgpu::RenderPipelineDescriptor pipelineDesc{};
  pipelineDesc.layout = pipelineLayout;
  pipelineDesc.vertex = vertexState;
  pipelineDesc.fragment = &fragmentState;
  pipelineDesc.primitive = primitive;

  wgpu::RenderPipeline pipeline = device.CreateRenderPipeline(&pipelineDesc);

  // Encode all mip downsample passes in a single command buffer.
  wgpu::CommandEncoder encoder = device.CreateCommandEncoder();

  // Copy mip 0 into the temporary cubemap to seed the chain.
  {
    wgpu::TexelCopyTextureInfo src{};
    src.texture = envCubemap;
    src.mipLevel = 0;
    src.origin = { 0, 0, 0 };
    src.aspect = wgpu::TextureAspect::All;

    wgpu::TexelCopyTextureInfo dst{};
    dst.texture = tempCubemap;
    dst.mipLevel = 0;
    dst.origin = { 0, 0, 0 };
    dst.aspect = wgpu::TextureAspect::All;

    wgpu::Extent3D extent{ envFaceSize, envFaceSize, 6 };
    encoder.CopyTextureToTexture(&src, &dst, &extent);
  }
  queue.Submit(1, &encoder.Finish());

  for (uint32_t mip = 1; mip < envMipCount; ++mip) {
    const uint32_t faceSize = envFaceSize >> mip;
    const float viewport = static_cast<float>(faceSize);

    DownsampleUniforms mipUniforms{};
    mipUniforms.sourceMip = static_cast<float>(mip - 1);
    queue.WriteBuffer(mipUbo, 0, &mipUniforms, sizeof(DownsampleUniforms));

    for (uint32_t face = 0; face < 6; ++face) {
      CaptureUniforms uniforms{};
      uniforms.viewProj = projection * views[face];
      DrawCubemapFace(device, queue, pipeline, bindGroup, captureUbo, uniforms, envCubemap, face, mip,
                      viewport);
    }

    wgpu::CommandEncoder copyEncoder = device.CreateCommandEncoder();
    wgpu::TexelCopyTextureInfo src{};
    src.texture = envCubemap;
    src.mipLevel = mip;
    src.origin = { 0, 0, 0 };
    src.aspect = wgpu::TextureAspect::All;

    wgpu::TexelCopyTextureInfo dst{};
    dst.texture = tempCubemap;
    dst.mipLevel = mip;
    dst.origin = { 0, 0, 0 };
    dst.aspect = wgpu::TextureAspect::All;

    wgpu::Extent3D extent{ faceSize, faceSize, 6 };
    copyEncoder.CopyTextureToTexture(&src, &dst, &extent);
    queue.Submit(1, &copyEncoder.Finish());
  }
}

} // namespace

IblTextures BakeDiffuseIrradiance(const wgpu::Device& device,
                                  const wgpu::Queue& queue,
                                  const io::HdrTexture& hdr,
                                  const IblBakeSettings& settings,
                                  IblBakeTimings* timingsOut,
                                  const wgpu::Instance* syncInstance) {
  IblTextures result{};
  result.bakedSettings = settings;
  const uint32_t envMipCount = EnvMipLevelCount(settings.envFaceSize);
  result.maxReflectionLod =
    static_cast<float>(settings.prefilterMipLevels > 0 ? settings.prefilterMipLevels - 1 : 0);

  // Shared linear clamp sampler for cubemap sampling.
  wgpu::SamplerDescriptor samplerDesc{};
  samplerDesc.addressModeU = wgpu::AddressMode::ClampToEdge;
  samplerDesc.addressModeV = wgpu::AddressMode::ClampToEdge;
  samplerDesc.addressModeW = wgpu::AddressMode::ClampToEdge;
  samplerDesc.magFilter = wgpu::FilterMode::Linear;
  samplerDesc.minFilter = wgpu::FilterMode::Linear;
  samplerDesc.mipmapFilter = wgpu::MipmapFilterMode::Linear;
  result.linearClampSampler = device.CreateSampler(&samplerDesc);

  // Create the environment cubemap (Step 3 output).
  wgpu::TextureDescriptor envDesc{};
  envDesc.size = { settings.envFaceSize, settings.envFaceSize, 6 };
  envDesc.mipLevelCount = envMipCount;
  envDesc.sampleCount = 1;
  envDesc.dimension = wgpu::TextureDimension::e2D;
  envDesc.format = wgpu::TextureFormat::RGBA16Float;
  envDesc.usage = wgpu::TextureUsage::TextureBinding
               | wgpu::TextureUsage::RenderAttachment
               | wgpu::TextureUsage::CopySrc
               | wgpu::TextureUsage::CopyDst;
  result.envCubemap = device.CreateTexture(&envDesc);

  // Create a cube view for sampling the environment map later (all mips).
  wgpu::TextureViewDescriptor envCubeViewDesc{};
  envCubeViewDesc.dimension = wgpu::TextureViewDimension::Cube;
  envCubeViewDesc.baseArrayLayer = 0;
  envCubeViewDesc.arrayLayerCount = 6;
  envCubeViewDesc.baseMipLevel = 0;
  envCubeViewDesc.mipLevelCount = envDesc.mipLevelCount;
  result.envCubemapView = result.envCubemap.CreateView(&envCubeViewDesc);

  // Uniform buffer for per-face view-projection matrices.
  wgpu::BufferDescriptor uboDesc{};
  uboDesc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
  uboDesc.size = sizeof(CaptureUniforms);
  wgpu::Buffer captureUbo = device.CreateBuffer(&uboDesc);

  // Load the equirect-to-cubemap shader.
  const std::string equirectCode = utils::LoadShaderFile("shaders/equirect_to_cube.wgsl");
  wgpu::ShaderModule equirectModule = utils::CreateWgslModule(device, equirectCode);
  if (!equirectModule) {
    return {};
  }

  // Bind group layout: uniform buffer + equirect texture (textureLoad, no sampler).
  wgpu::BindGroupLayoutEntry uboEntry{};
  uboEntry.binding = 0;
  uboEntry.visibility = wgpu::ShaderStage::Vertex;
  uboEntry.buffer.type = wgpu::BufferBindingType::Uniform;

  wgpu::BindGroupLayoutEntry texEntry{};
  texEntry.binding = 1;
  texEntry.visibility = wgpu::ShaderStage::Fragment;
  texEntry.texture.sampleType = wgpu::TextureSampleType::UnfilterableFloat;
  texEntry.texture.viewDimension = wgpu::TextureViewDimension::e2D;

  wgpu::BindGroupLayoutEntry bglEntries[2] = { uboEntry, texEntry };
  wgpu::BindGroupLayoutDescriptor bglDesc{};
  bglDesc.entryCount = 2;
  bglDesc.entries = bglEntries;
  wgpu::BindGroupLayout bgl = device.CreateBindGroupLayout(&bglDesc);

  wgpu::BindGroupEntry uboBind{};
  uboBind.binding = 0;
  uboBind.buffer = captureUbo;
  uboBind.offset = 0;
  uboBind.size = sizeof(CaptureUniforms);

  wgpu::BindGroupEntry texBind{};
  texBind.binding = 1;
  texBind.textureView = hdr.view;

  wgpu::BindGroupEntry bgEntries[2] = { uboBind, texBind };
  wgpu::BindGroupDescriptor bgDesc{};
  bgDesc.layout = bgl;
  bgDesc.entryCount = 2;
  bgDesc.entries = bgEntries;
  wgpu::BindGroup equirectBindGroup = device.CreateBindGroup(&bgDesc);

  // Pipeline layout (single bind group).
  wgpu::PipelineLayoutDescriptor plDesc{};
  plDesc.bindGroupLayoutCount = 1;
  plDesc.bindGroupLayouts = &bgl;
  wgpu::PipelineLayout pipelineLayout = device.CreatePipelineLayout(&plDesc);

  // Color target for the env cubemap.
  wgpu::ColorTargetState colorTarget{};
  colorTarget.format = wgpu::TextureFormat::RGBA16Float;
  colorTarget.writeMask = wgpu::ColorWriteMask::All;

  wgpu::FragmentState fragmentState{};
  fragmentState.module = equirectModule;
  fragmentState.entryPoint = "fs_main";
  fragmentState.targetCount = 1;
  fragmentState.targets = &colorTarget;

  wgpu::VertexState vertexState{};
  vertexState.module = equirectModule;
  vertexState.entryPoint = "vs_main";
  vertexState.bufferCount = 0; // We use vertex_index inside WGSL.

  wgpu::PrimitiveState primitive{};
  primitive.topology = wgpu::PrimitiveTopology::TriangleList;
  primitive.cullMode = wgpu::CullMode::None; // We render all cube faces.

  wgpu::RenderPipelineDescriptor pipelineDesc{};
  pipelineDesc.layout = pipelineLayout;
  pipelineDesc.vertex = vertexState;
  pipelineDesc.fragment = &fragmentState;
  pipelineDesc.primitive = primitive;

  wgpu::RenderPipeline equirectPipeline = device.CreateRenderPipeline(&pipelineDesc);

  const glm::mat4 projection = MakeCaptureProjection();
  const auto views = BuildCaptureViews();

  const auto tEquirectStart = std::chrono::steady_clock::now();
  for (uint32_t face = 0; face < 6; ++face) {
    CaptureUniforms uniforms{};
    uniforms.viewProj = projection * views[face];
    DrawCubemapFace(device, queue, equirectPipeline, equirectBindGroup, captureUbo, uniforms,
                    result.envCubemap, face, 0);
  }
  SyncIfTiming(syncInstance, queue, timingsOut);
  if (timingsOut) {
    timingsOut->equirectMs = ElapsedMs(tEquirectStart);
  }

  // Step 4: build the diffuse irradiance cubemap (32x32, 6 faces).
  wgpu::BufferDescriptor irrParamsDesc{};
  irrParamsDesc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
  irrParamsDesc.size = sizeof(IrradianceUniforms);
  wgpu::Buffer irrParamsUbo = device.CreateBuffer(&irrParamsDesc);
  IrradianceUniforms irrParams{};
  irrParams.sampleDelta = settings.irradianceSampleDelta;
  queue.WriteBuffer(irrParamsUbo, 0, &irrParams, sizeof(IrradianceUniforms));

  wgpu::TextureDescriptor irradianceDesc{};
  irradianceDesc.size = { settings.irradianceFaceSize, settings.irradianceFaceSize, 6 };
  irradianceDesc.mipLevelCount = 1;
  irradianceDesc.sampleCount = 1;
  irradianceDesc.dimension = wgpu::TextureDimension::e2D;
  irradianceDesc.format = wgpu::TextureFormat::RGBA16Float;
  irradianceDesc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::RenderAttachment;
  result.irradianceCubemap = device.CreateTexture(&irradianceDesc);

  wgpu::TextureViewDescriptor irradianceViewDesc{};
  irradianceViewDesc.dimension = wgpu::TextureViewDimension::Cube;
  irradianceViewDesc.baseArrayLayer = 0;
  irradianceViewDesc.arrayLayerCount = 6;
  irradianceViewDesc.baseMipLevel = 0;
  irradianceViewDesc.mipLevelCount = 1;
  result.irradianceView = result.irradianceCubemap.CreateView(&irradianceViewDesc);

  // Load the irradiance convolution shader.
  const std::string irradianceCode = utils::LoadShaderFile("shaders/irradiance_convolution.wgsl");
  wgpu::ShaderModule irradianceModule = utils::CreateWgslModule(device, irradianceCode);
  if (!irradianceModule) {
    return result;
  }

  // Bind group layout: uniform buffer + cubemap + sampler.
  wgpu::BindGroupLayoutEntry irrUboEntry{};
  irrUboEntry.binding = 0;
  irrUboEntry.visibility = wgpu::ShaderStage::Vertex;
  irrUboEntry.buffer.type = wgpu::BufferBindingType::Uniform;

  wgpu::BindGroupLayoutEntry irrTexEntry{};
  irrTexEntry.binding = 1;
  irrTexEntry.visibility = wgpu::ShaderStage::Fragment;
  irrTexEntry.texture.sampleType = wgpu::TextureSampleType::Float;
  irrTexEntry.texture.viewDimension = wgpu::TextureViewDimension::Cube;

  wgpu::BindGroupLayoutEntry irrSamplerEntry{};
  irrSamplerEntry.binding = 2;
  irrSamplerEntry.visibility = wgpu::ShaderStage::Fragment;
  irrSamplerEntry.sampler.type = wgpu::SamplerBindingType::Filtering;

  wgpu::BindGroupLayoutEntry irrParamsEntry{};
  irrParamsEntry.binding = 3;
  irrParamsEntry.visibility = wgpu::ShaderStage::Fragment;
  irrParamsEntry.buffer.type = wgpu::BufferBindingType::Uniform;

  wgpu::BindGroupLayoutEntry irrEntries[4] = { irrUboEntry, irrTexEntry, irrSamplerEntry, irrParamsEntry };
  wgpu::BindGroupLayoutDescriptor irrBglDesc{};
  irrBglDesc.entryCount = 4;
  irrBglDesc.entries = irrEntries;
  wgpu::BindGroupLayout irrBgl = device.CreateBindGroupLayout(&irrBglDesc);

  wgpu::BindGroupEntry irrUboBind{};
  irrUboBind.binding = 0;
  irrUboBind.buffer = captureUbo;
  irrUboBind.offset = 0;
  irrUboBind.size = sizeof(CaptureUniforms);

  wgpu::BindGroupEntry irrTexBind{};
  irrTexBind.binding = 1;
  irrTexBind.textureView = result.envCubemapView;

  wgpu::BindGroupEntry irrSamplerBind{};
  irrSamplerBind.binding = 2;
  irrSamplerBind.sampler = result.linearClampSampler;

  wgpu::BindGroupEntry irrParamsBind{};
  irrParamsBind.binding = 3;
  irrParamsBind.buffer = irrParamsUbo;
  irrParamsBind.offset = 0;
  irrParamsBind.size = sizeof(IrradianceUniforms);

  wgpu::BindGroupEntry irrBgEntries[4] = { irrUboBind, irrTexBind, irrSamplerBind, irrParamsBind };
  wgpu::BindGroupDescriptor irrBgDesc{};
  irrBgDesc.layout = irrBgl;
  irrBgDesc.entryCount = 4;
  irrBgDesc.entries = irrBgEntries;
  wgpu::BindGroup irradianceBindGroup = device.CreateBindGroup(&irrBgDesc);

  wgpu::PipelineLayoutDescriptor irrPlDesc{};
  irrPlDesc.bindGroupLayoutCount = 1;
  irrPlDesc.bindGroupLayouts = &irrBgl;
  wgpu::PipelineLayout irrPipelineLayout = device.CreatePipelineLayout(&irrPlDesc);

  wgpu::ColorTargetState irrColorTarget{};
  irrColorTarget.format = wgpu::TextureFormat::RGBA16Float;
  irrColorTarget.writeMask = wgpu::ColorWriteMask::All;

  wgpu::FragmentState irrFragment{};
  irrFragment.module = irradianceModule;
  irrFragment.entryPoint = "fs_main";
  irrFragment.targetCount = 1;
  irrFragment.targets = &irrColorTarget;

  wgpu::VertexState irrVertex{};
  irrVertex.module = irradianceModule;
  irrVertex.entryPoint = "vs_main";
  irrVertex.bufferCount = 0;

  wgpu::RenderPipelineDescriptor irrPipelineDesc{};
  irrPipelineDesc.layout = irrPipelineLayout;
  irrPipelineDesc.vertex = irrVertex;
  irrPipelineDesc.fragment = &irrFragment;
  irrPipelineDesc.primitive = primitive;

  wgpu::RenderPipeline irradiancePipeline = device.CreateRenderPipeline(&irrPipelineDesc);

  const auto tIrradianceStart = std::chrono::steady_clock::now();
  for (uint32_t face = 0; face < 6; ++face) {
    CaptureUniforms uniforms{};
    uniforms.viewProj = projection * views[face];
    DrawCubemapFace(device, queue, irradiancePipeline, irradianceBindGroup, captureUbo, uniforms,
                    result.irradianceCubemap, face, 0);
  }
  SyncIfTiming(syncInstance, queue, timingsOut);
  if (timingsOut) {
    timingsOut->irradianceMs = ElapsedMs(tIrradianceStart);
  }

  return result;
}

void BakeSpecularPrefilter(const wgpu::Device& device,
                           const wgpu::Queue& queue,
                           IblTextures& textures,
                           const IblBakeSettings& settings,
                           IblBakeTimings* timingsOut,
                           const wgpu::Instance* syncInstance) {
  textures.bakedSettings = settings;
  textures.maxReflectionLod =
    static_cast<float>(settings.prefilterMipLevels > 0 ? settings.prefilterMipLevels - 1 : 0);
  const glm::mat4 projection = MakeCaptureProjection();
  const auto views = BuildCaptureViews();

  // Reuse a view-projection uniform buffer.
  wgpu::BufferDescriptor uboDesc{};
  uboDesc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
  uboDesc.size = sizeof(CaptureUniforms);
  wgpu::Buffer captureUbo = device.CreateBuffer(&uboDesc);

  const auto tEnvMipsStart = std::chrono::steady_clock::now();
  GenerateEnvCubemapMips(device, queue, textures.envCubemap, textures.envCubemapView,
                         textures.linearClampSampler, captureUbo, projection, views, settings);
  SyncIfTiming(syncInstance, queue, timingsOut);
  if (timingsOut) {
    timingsOut->envMipsMs = ElapsedMs(tEnvMipsStart);
  }

  wgpu::TextureDescriptor prefilterDesc{};
  prefilterDesc.size = { settings.prefilterFaceSize, settings.prefilterFaceSize, 6 };
  prefilterDesc.mipLevelCount = settings.prefilterMipLevels;
  prefilterDesc.sampleCount = 1;
  prefilterDesc.dimension = wgpu::TextureDimension::e2D;
  prefilterDesc.format = wgpu::TextureFormat::RGBA16Float;
  prefilterDesc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::RenderAttachment;
  textures.prefilterCubemap = device.CreateTexture(&prefilterDesc);

  wgpu::TextureViewDescriptor prefilterViewDesc{};
  prefilterViewDesc.dimension = wgpu::TextureViewDimension::Cube;
  prefilterViewDesc.baseArrayLayer = 0;
  prefilterViewDesc.arrayLayerCount = 6;
  prefilterViewDesc.baseMipLevel = 0;
  prefilterViewDesc.mipLevelCount = prefilterDesc.mipLevelCount;
  textures.prefilterView = textures.prefilterCubemap.CreateView(&prefilterViewDesc);

  // Load the prefilter shader.
  const std::string prefilterCode = utils::LoadShaderFile("shaders/prefilter_env.wgsl");
  wgpu::ShaderModule prefilterModule = utils::CreateWgslModule(device, prefilterCode);
  if (!prefilterModule) {
    return;
  }

  // Uniform buffer for roughness + resolution.
  wgpu::BufferDescriptor prefilterUboDesc{};
  prefilterUboDesc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
  prefilterUboDesc.size = sizeof(PrefilterUniforms);
  wgpu::Buffer prefilterUbo = device.CreateBuffer(&prefilterUboDesc);

  // Bind group layout: viewProj + env cube + sampler + prefilter uniforms.
  wgpu::BindGroupLayoutEntry viewEntry{};
  viewEntry.binding = 0;
  viewEntry.visibility = wgpu::ShaderStage::Vertex;
  viewEntry.buffer.type = wgpu::BufferBindingType::Uniform;

  wgpu::BindGroupLayoutEntry envEntry{};
  envEntry.binding = 1;
  envEntry.visibility = wgpu::ShaderStage::Fragment;
  envEntry.texture.sampleType = wgpu::TextureSampleType::Float;
  envEntry.texture.viewDimension = wgpu::TextureViewDimension::Cube;

  wgpu::BindGroupLayoutEntry samplerEntry{};
  samplerEntry.binding = 2;
  samplerEntry.visibility = wgpu::ShaderStage::Fragment;
  samplerEntry.sampler.type = wgpu::SamplerBindingType::Filtering;

  wgpu::BindGroupLayoutEntry prefilterEntry{};
  prefilterEntry.binding = 3;
  prefilterEntry.visibility = wgpu::ShaderStage::Fragment;
  prefilterEntry.buffer.type = wgpu::BufferBindingType::Uniform;

  wgpu::BindGroupLayoutEntry bglEntries[4] = { viewEntry, envEntry, samplerEntry, prefilterEntry };
  wgpu::BindGroupLayoutDescriptor bglDesc{};
  bglDesc.entryCount = 4;
  bglDesc.entries = bglEntries;
  wgpu::BindGroupLayout bgl = device.CreateBindGroupLayout(&bglDesc);

  wgpu::BindGroupEntry viewBind{};
  viewBind.binding = 0;
  viewBind.buffer = captureUbo;
  viewBind.offset = 0;
  viewBind.size = sizeof(CaptureUniforms);

  wgpu::BindGroupEntry envBind{};
  envBind.binding = 1;
  envBind.textureView = textures.envCubemapView;

  wgpu::BindGroupEntry samplerBind{};
  samplerBind.binding = 2;
  samplerBind.sampler = textures.linearClampSampler;

  wgpu::BindGroupEntry prefilterBind{};
  prefilterBind.binding = 3;
  prefilterBind.buffer = prefilterUbo;
  prefilterBind.offset = 0;
  prefilterBind.size = sizeof(PrefilterUniforms);

  wgpu::BindGroupEntry bgEntries[4] = { viewBind, envBind, samplerBind, prefilterBind };
  wgpu::BindGroupDescriptor bgDesc{};
  bgDesc.layout = bgl;
  bgDesc.entryCount = 4;
  bgDesc.entries = bgEntries;
  wgpu::BindGroup bindGroup = device.CreateBindGroup(&bgDesc);

  wgpu::PipelineLayoutDescriptor plDesc{};
  plDesc.bindGroupLayoutCount = 1;
  plDesc.bindGroupLayouts = &bgl;
  wgpu::PipelineLayout pipelineLayout = device.CreatePipelineLayout(&plDesc);

  wgpu::ColorTargetState colorTarget{};
  colorTarget.format = wgpu::TextureFormat::RGBA16Float;
  colorTarget.writeMask = wgpu::ColorWriteMask::All;

  wgpu::FragmentState fragmentState{};
  fragmentState.module = prefilterModule;
  fragmentState.entryPoint = "fs_main";
  fragmentState.targetCount = 1;
  fragmentState.targets = &colorTarget;

  wgpu::VertexState vertexState{};
  vertexState.module = prefilterModule;
  vertexState.entryPoint = "vs_main";
  vertexState.bufferCount = 0;

  wgpu::PrimitiveState primitive{};
  primitive.topology = wgpu::PrimitiveTopology::TriangleList;
  primitive.cullMode = wgpu::CullMode::None;

  wgpu::RenderPipelineDescriptor pipelineDesc{};
  pipelineDesc.layout = pipelineLayout;
  pipelineDesc.vertex = vertexState;
  pipelineDesc.fragment = &fragmentState;
  pipelineDesc.primitive = primitive;

  wgpu::RenderPipeline pipeline = device.CreateRenderPipeline(&pipelineDesc);

  const uint32_t mipLevels = settings.prefilterMipLevels;
  const float roughnessDenom = mipLevels > 1 ? static_cast<float>(mipLevels - 1) : 1.0f;

  const auto tPrefilterStart = std::chrono::steady_clock::now();
  for (uint32_t mip = 0; mip < mipLevels; ++mip) {
    const float roughness = static_cast<float>(mip) / roughnessDenom;
    const uint32_t faceSize = settings.prefilterFaceSize >> mip;
    const float viewport = static_cast<float>(faceSize);

    PrefilterUniforms prefilterUniforms{};
    prefilterUniforms.roughness = roughness;
    prefilterUniforms.resolution = static_cast<float>(settings.envFaceSize);
    prefilterUniforms.sampleCount = static_cast<float>(settings.prefilterSampleCount);
    queue.WriteBuffer(prefilterUbo, 0, &prefilterUniforms, sizeof(PrefilterUniforms));

    for (uint32_t face = 0; face < 6; ++face) {
      CaptureUniforms uniforms{};
      uniforms.viewProj = projection * views[face];
      DrawCubemapFace(device, queue, pipeline, bindGroup, captureUbo, uniforms, textures.prefilterCubemap,
                      face, mip, viewport);
    }
  }
  SyncIfTiming(syncInstance, queue, timingsOut);
  if (timingsOut) {
    timingsOut->prefilterMs = ElapsedMs(tPrefilterStart);
  }
}

void BakeBrdfLut(const wgpu::Device& device,
                 const wgpu::Queue& queue,
                 IblTextures& textures,
                 const IblBakeSettings& settings,
                 IblBakeTimings* timingsOut,
                 const wgpu::Instance* syncInstance) {
  // Create the BRDF LUT texture as RG32Float with storage + sampling usage.
  // Dawn does not allow RG16Float for storage textures on this backend.
  wgpu::TextureDescriptor lutDesc{};
  lutDesc.size = { 512, 512, 1 };
  lutDesc.mipLevelCount = 1;
  lutDesc.sampleCount = 1;
  lutDesc.dimension = wgpu::TextureDimension::e2D;
  lutDesc.format = wgpu::TextureFormat::RG32Float;
  lutDesc.usage = wgpu::TextureUsage::StorageBinding | wgpu::TextureUsage::TextureBinding;
  textures.brdfLut = device.CreateTexture(&lutDesc);

  // Create a 2D view for sampling in the runtime shader.
  textures.brdfLutView = textures.brdfLut.CreateView();

  // Load the BRDF LUT compute shader.
  const std::string brdfCode = utils::LoadShaderFile("shaders/brdf_lut.wgsl");
  wgpu::ShaderModule brdfModule = utils::CreateWgslModule(device, brdfCode);
  if (!brdfModule) {
    return;
  }

  wgpu::BufferDescriptor brdfUboDesc{};
  brdfUboDesc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
  brdfUboDesc.size = sizeof(BrdfUniforms);
  wgpu::Buffer brdfUbo = device.CreateBuffer(&brdfUboDesc);
  BrdfUniforms brdfUniforms{};
  brdfUniforms.sampleCount = static_cast<float>(settings.brdfSampleCount);
  queue.WriteBuffer(brdfUbo, 0, &brdfUniforms, sizeof(BrdfUniforms));

  wgpu::BindGroupLayoutEntry lutEntry{};
  lutEntry.binding = 0;
  lutEntry.visibility = wgpu::ShaderStage::Compute;
  lutEntry.storageTexture.access = wgpu::StorageTextureAccess::WriteOnly;
  lutEntry.storageTexture.format = wgpu::TextureFormat::RG32Float;
  lutEntry.storageTexture.viewDimension = wgpu::TextureViewDimension::e2D;

  wgpu::BindGroupLayoutEntry brdfParamsEntry{};
  brdfParamsEntry.binding = 1;
  brdfParamsEntry.visibility = wgpu::ShaderStage::Compute;
  brdfParamsEntry.buffer.type = wgpu::BufferBindingType::Uniform;

  wgpu::BindGroupLayoutEntry bglEntries[2] = { lutEntry, brdfParamsEntry };
  wgpu::BindGroupLayoutDescriptor bglDesc{};
  bglDesc.entryCount = 2;
  bglDesc.entries = bglEntries;
  wgpu::BindGroupLayout bgl = device.CreateBindGroupLayout(&bglDesc);

  wgpu::BindGroupEntry lutBind{};
  lutBind.binding = 0;
  lutBind.textureView = textures.brdfLutView;

  wgpu::BindGroupEntry brdfParamsBind{};
  brdfParamsBind.binding = 1;
  brdfParamsBind.buffer = brdfUbo;
  brdfParamsBind.offset = 0;
  brdfParamsBind.size = sizeof(BrdfUniforms);

  wgpu::BindGroupEntry bgEntries[2] = { lutBind, brdfParamsBind };
  wgpu::BindGroupDescriptor bgDesc{};
  bgDesc.layout = bgl;
  bgDesc.entryCount = 2;
  bgDesc.entries = bgEntries;
  wgpu::BindGroup bindGroup = device.CreateBindGroup(&bgDesc);

  // Pipeline layout and compute pipeline.
  wgpu::PipelineLayoutDescriptor plDesc{};
  plDesc.bindGroupLayoutCount = 1;
  plDesc.bindGroupLayouts = &bgl;
  wgpu::PipelineLayout pipelineLayout = device.CreatePipelineLayout(&plDesc);

  wgpu::ComputePipelineDescriptor pipeDesc{};
  pipeDesc.layout = pipelineLayout;
  pipeDesc.compute.module = brdfModule;
  pipeDesc.compute.entryPoint = "cs_main";
  wgpu::ComputePipeline pipeline = device.CreateComputePipeline(&pipeDesc);

  // Encode compute dispatch (512x512 with 16x16 workgroups).
  const auto tBrdfStart = std::chrono::steady_clock::now();
  wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
  wgpu::ComputePassEncoder pass = encoder.BeginComputePass();
  pass.SetPipeline(pipeline);
  pass.SetBindGroup(0, bindGroup);
  pass.DispatchWorkgroups(512 / 16, 512 / 16, 1);
  pass.End();

  queue.Submit(1, &encoder.Finish());
  SyncIfTiming(syncInstance, queue, timingsOut);
  if (timingsOut) {
    timingsOut->brdfMs = ElapsedMs(tBrdfStart);
  }
}

} // namespace ibl
