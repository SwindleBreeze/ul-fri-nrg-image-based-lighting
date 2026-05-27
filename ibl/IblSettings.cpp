#include "ibl/IblBaker.h"

#include <cmath>

namespace ibl {

uint32_t EnvMipLevelCount(uint32_t envFaceSize) {
  if (envFaceSize <= 1) {
    return 1;
  }
  const uint32_t levels = static_cast<uint32_t>(std::floor(std::log2(static_cast<double>(envFaceSize)))) + 1;
  return levels < 1 ? 1 : levels;
}

IblBakeSettings SettingsForPreset(IblQualityPreset preset) {
  IblBakeSettings s{};
  switch (preset) {
    case IblQualityPreset::Low:
      s.envFaceSize = 256;
      s.irradianceFaceSize = 16;
      s.prefilterFaceSize = 64;
      s.prefilterMipLevels = 4;
      s.prefilterSampleCount = 256;
      s.brdfSampleCount = 256;
      s.irradianceSampleDelta = 0.05f;
      break;
    case IblQualityPreset::High:
      s.envFaceSize = 512;
      s.irradianceFaceSize = 64;
      s.prefilterFaceSize = 256;
      s.prefilterMipLevels = 6;
      s.prefilterSampleCount = 4096;
      s.brdfSampleCount = 4096;
      s.irradianceSampleDelta = 0.0125f;
      break;
    case IblQualityPreset::Medium:
    default:
      break;
  }
  return s;
}

bool IblTexturesReady(const IblTextures& textures) {
  return textures.envCubemapView && textures.irradianceView && textures.prefilterView &&
         textures.brdfLutView && textures.linearClampSampler;
}

void DestroyIblTextures(IblTextures& textures) {
  textures.envCubemap = {};
  textures.envCubemapView = {};
  textures.irradianceCubemap = {};
  textures.irradianceView = {};
  textures.prefilterCubemap = {};
  textures.prefilterView = {};
  textures.brdfLut = {};
  textures.brdfLutView = {};
  textures.linearClampSampler = {};
}

} // namespace ibl
