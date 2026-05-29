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

  bool screenshotRequested = false;
  std::string lastScreenshotPath;

  // After rebake or preset change, log FPS once the rolling window is full.
  bool collectFpsSample = false;
  ibl::IblQualityPreset fpsSamplePreset = ibl::IblQualityPreset::Medium;
  bool fpsLoggedSinceRebake = false;
};

} // namespace app
