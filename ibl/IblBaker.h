#pragma once

#include <cstdint>

#include <webgpu/webgpu_cpp.h>

#include "io/HdrLoader.h"

namespace ibl {

enum class IblQualityPreset { Low, Medium, High };

struct IblBakeSettings {
  uint32_t envFaceSize = 512;
  uint32_t irradianceFaceSize = 32;
  uint32_t prefilterFaceSize = 128;
  uint32_t prefilterMipLevels = 5;
  uint32_t prefilterSampleCount = 1024;
  uint32_t brdfSampleCount = 1024;
  float irradianceSampleDelta = 0.025f;
};

struct IblBakeTimings {
  double equirectMs = 0.0;
  double irradianceMs = 0.0;
  double envMipsMs = 0.0;
  double prefilterMs = 0.0;
  double brdfMs = 0.0;
  double totalMs = 0.0;
};

struct IblTextures {
  wgpu::Texture envCubemap;
  wgpu::TextureView envCubemapView;
  wgpu::Texture irradianceCubemap;
  wgpu::TextureView irradianceView;
  wgpu::Texture prefilterCubemap;
  wgpu::TextureView prefilterView;
  wgpu::Texture brdfLut;
  wgpu::TextureView brdfLutView;
  wgpu::Sampler linearClampSampler;
  float maxReflectionLod = 4.0f;
  IblBakeSettings bakedSettings{};
};

uint32_t EnvMipLevelCount(uint32_t envFaceSize);
IblBakeSettings SettingsForPreset(IblQualityPreset preset);

void DestroyIblTextures(IblTextures& textures);

bool IblTexturesReady(const IblTextures& textures);

// When timingsOut is non-null, syncInstance must be set so GPU work is included in timings.
IblTextures BakeDiffuseIrradiance(const wgpu::Device& device,
                                  const wgpu::Queue& queue,
                                  const io::HdrTexture& hdr,
                                  const IblBakeSettings& settings,
                                  IblBakeTimings* timingsOut = nullptr,
                                  const wgpu::Instance* syncInstance = nullptr);

void BakeSpecularPrefilter(const wgpu::Device& device,
                           const wgpu::Queue& queue,
                           IblTextures& textures,
                           const IblBakeSettings& settings,
                           IblBakeTimings* timingsOut = nullptr,
                           const wgpu::Instance* syncInstance = nullptr);

void BakeBrdfLut(const wgpu::Device& device,
                 const wgpu::Queue& queue,
                 IblTextures& textures,
                 const IblBakeSettings& settings,
                 IblBakeTimings* timingsOut = nullptr,
                 const wgpu::Instance* syncInstance = nullptr);

} // namespace ibl
