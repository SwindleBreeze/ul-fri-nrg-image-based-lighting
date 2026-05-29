#pragma once

#include <cstdint>

#include "ibl/IblBaker.h"

namespace app {

// All paths are under results/ relative to the process working directory.

void LogBakeTimings(ibl::IblQualityPreset preset, const ibl::IblBakeTimings& timings);

void LogRuntimeFps(ibl::IblQualityPreset preset,
                   float fpsAvg,
                   float fpsMin,
                   uint32_t width,
                   uint32_t height,
                   const char* trigger);

void RegenerateEvaluationSummary();

} // namespace app
