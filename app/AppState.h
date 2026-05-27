#pragma once

#include <string>

#include "ibl/IblBaker.h"

namespace app {

struct AppState {
  ibl::IblBakeSettings bakeSettings = ibl::SettingsForPreset(ibl::IblQualityPreset::Medium);
  ibl::IblBakeTimings bakeTimings{};
  ibl::IblQualityPreset qualityPreset = ibl::IblQualityPreset::Medium;

  int selectedObjectIndex = 0;
  float envYawDegrees = 0.0f;

  bool rebakeRequested = false;
  bool rebaking = false;

  float fpsAvg = 0.0f;
  float fpsMin = 0.0f;
};

} // namespace app
