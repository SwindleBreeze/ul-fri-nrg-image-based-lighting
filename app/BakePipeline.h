#pragma once

#include <webgpu/webgpu_cpp.h>

#include "ibl/IblBaker.h"
#include "io/HdrLoader.h"

namespace app {

void WaitForQueue(const wgpu::Instance& instance, const wgpu::Queue& queue);

ibl::IblTextures RunIblBake(const wgpu::Instance& instance,
                            const wgpu::Device& device,
                            const wgpu::Queue& queue,
                            const io::HdrTexture& hdr,
                            const ibl::IblBakeSettings& settings,
                            ibl::IblBakeTimings& timingsOut,
                            ibl::IblQualityPreset presetForLog = ibl::IblQualityPreset::Medium);

} // namespace app
