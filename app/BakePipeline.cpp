#include "app/BakePipeline.h"

#include <iostream>

#include "app/EvaluationLog.h"
#include "utils/QueueSync.h"

namespace app {

void WaitForQueue(const wgpu::Instance& instance, const wgpu::Queue& queue) {
  utils::WaitForQueue(instance, queue);
}

ibl::IblTextures RunIblBake(const wgpu::Instance& instance,
                            const wgpu::Device& device,
                            const wgpu::Queue& queue,
                            const io::HdrTexture& hdr,
                            const ibl::IblBakeSettings& settings,
                            ibl::IblBakeTimings& timingsOut,
                            ibl::IblQualityPreset presetForLog) {
  timingsOut = {};
  const wgpu::Instance* syncInstance = &instance;

  ibl::IblTextures textures =
    ibl::BakeDiffuseIrradiance(device, queue, hdr, settings, &timingsOut, syncInstance);

  ibl::BakeSpecularPrefilter(device, queue, textures, settings, &timingsOut, syncInstance);

  ibl::BakeBrdfLut(device, queue, textures, settings, &timingsOut, syncInstance);

  timingsOut.totalMs = timingsOut.equirectMs + timingsOut.irradianceMs + timingsOut.envMipsMs +
                       timingsOut.prefilterMs + timingsOut.brdfMs;

  std::cout << "IBL bake timings (ms): equirect=" << timingsOut.equirectMs
            << " irradiance=" << timingsOut.irradianceMs
            << " envMips=" << timingsOut.envMipsMs
            << " prefilter=" << timingsOut.prefilterMs
            << " brdf=" << timingsOut.brdfMs
            << " total=" << timingsOut.totalMs << "\n";

  LogBakeTimings(presetForLog, timingsOut);

  return textures;
}

} // namespace app
