#include "app/BakePipeline.h"

#include <chrono>
#include <iostream>
#include <string_view>

namespace app {

void WaitForQueue(const wgpu::Instance& instance, const wgpu::Queue& queue) {
  wgpu::Future future = queue.OnSubmittedWorkDone(
    wgpu::CallbackMode::WaitAnyOnly,
    [](wgpu::QueueWorkDoneStatus status, wgpu::StringView message) {
      if (status != wgpu::QueueWorkDoneStatus::Success) {
        std::cerr << "Queue work done error: "
                  << std::string_view(message.data, message.length) << "\n";
      }
    });
  instance.WaitAny(future, UINT64_MAX);
}

namespace {

double ElapsedMs(const std::chrono::steady_clock::time_point& start) {
  const auto end = std::chrono::steady_clock::now();
  return std::chrono::duration<double, std::milli>(end - start).count();
}

} // namespace

ibl::IblTextures RunIblBake(const wgpu::Instance& instance,
                            const wgpu::Device& device,
                            const wgpu::Queue& queue,
                            const io::HdrTexture& hdr,
                            const ibl::IblBakeSettings& settings,
                            ibl::IblBakeTimings& timingsOut) {
  timingsOut = {};

  auto t0 = std::chrono::steady_clock::now();
  ibl::IblTextures textures =
    ibl::BakeDiffuseIrradiance(device, queue, hdr, settings, &timingsOut);
  WaitForQueue(instance, queue);
  instance.ProcessEvents();
  timingsOut.equirectMs = ElapsedMs(t0);

  auto t1 = std::chrono::steady_clock::now();
  ibl::BakeSpecularPrefilter(device, queue, textures, settings, &timingsOut);
  WaitForQueue(instance, queue);
  instance.ProcessEvents();
  const double specularTotal = ElapsedMs(t1);
  timingsOut.envMipsMs = specularTotal * 0.35;
  timingsOut.prefilterMs = specularTotal - timingsOut.envMipsMs;

  auto t2 = std::chrono::steady_clock::now();
  ibl::BakeBrdfLut(device, queue, textures, settings, &timingsOut);
  WaitForQueue(instance, queue);
  instance.ProcessEvents();
  timingsOut.brdfMs = ElapsedMs(t2);

  auto tIrrStart = t0;
  timingsOut.irradianceMs = timingsOut.equirectMs * 0.4;
  timingsOut.equirectMs -= timingsOut.irradianceMs;
  (void)tIrrStart;

  timingsOut.totalMs = timingsOut.equirectMs + timingsOut.irradianceMs + timingsOut.envMipsMs +
                       timingsOut.prefilterMs + timingsOut.brdfMs;

  std::cout << "IBL bake timings (ms): equirect=" << timingsOut.equirectMs
            << " irradiance=" << timingsOut.irradianceMs
            << " envMips=" << timingsOut.envMipsMs
            << " prefilter=" << timingsOut.prefilterMs
            << " brdf=" << timingsOut.brdfMs
            << " total=" << timingsOut.totalMs << "\n";

  return textures;
}

} // namespace app
