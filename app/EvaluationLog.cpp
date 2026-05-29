#include "app/EvaluationLog.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace app {

namespace {

const char* PresetName(ibl::IblQualityPreset preset) {
  switch (preset) {
    case ibl::IblQualityPreset::Low:
      return "Low";
    case ibl::IblQualityPreset::High:
      return "High";
    default:
      return "Medium";
  }
}

std::filesystem::path ResultsDir() {
  return std::filesystem::path("results");
}

void AppendCsvHeaderIfNeeded(const std::filesystem::path& path, const char* header) {
  if (!std::filesystem::exists(path) || std::filesystem::file_size(path) == 0) {
    std::ofstream out(path, std::ios::app);
    if (out) {
      out << header << '\n';
    }
  }
}

} // namespace

void LogBakeTimings(ibl::IblQualityPreset preset, const ibl::IblBakeTimings& timings) {
  namespace fs = std::filesystem;
  const fs::path csvPath = ResultsDir() / "bake_timings.csv";
  fs::create_directories(csvPath.parent_path());

  AppendCsvHeaderIfNeeded(csvPath,
                          "preset,equirect_ms,irradiance_ms,env_mips_ms,prefilter_ms,brdf_ms,"
                          "total_ms");

  std::ofstream out(csvPath, std::ios::app);
  if (!out) {
    return;
  }

  out << PresetName(preset) << ','
      << timings.equirectMs << ','
      << timings.irradianceMs << ','
      << timings.envMipsMs << ','
      << timings.prefilterMs << ','
      << timings.brdfMs << ','
      << timings.totalMs << '\n';

  std::cout << "Bake timings CSV: " << fs::absolute(csvPath).string() << '\n';
  RegenerateEvaluationSummary();
}

void LogRuntimeFps(ibl::IblQualityPreset preset,
                   float fpsAvg,
                   float fpsMin,
                   uint32_t width,
                   uint32_t height,
                   const char* trigger) {
  namespace fs = std::filesystem;
  const fs::path csvPath = ResultsDir() / "runtime_fps.csv";
  fs::create_directories(csvPath.parent_path());

  AppendCsvHeaderIfNeeded(csvPath,
                          "preset,avg_fps,min_fps,frame_ms_avg,width,height,trigger");

  const float frameMsAvg = fpsAvg > 0.0f ? 1000.0f / fpsAvg : 0.0f;

  std::ofstream out(csvPath, std::ios::app);
  if (!out) {
    return;
  }

  out << PresetName(preset) << ','
      << fpsAvg << ','
      << fpsMin << ','
      << frameMsAvg << ','
      << width << ','
      << height << ','
      << trigger << '\n';

  std::cout << "Runtime FPS CSV: " << fs::absolute(csvPath).string()
            << " (" << PresetName(preset) << " avg=" << fpsAvg << " min=" << fpsMin << ")\n";
  RegenerateEvaluationSummary();
}

void RegenerateEvaluationSummary() {
  namespace fs = std::filesystem;
  const fs::path summaryPath = ResultsDir() / "evaluation_summary.json";
  fs::create_directories(summaryPath.parent_path());

  std::ostringstream json;
  json << std::fixed << std::setprecision(4);
  json << "{\n";
  json << "  \"results_directory\": \"" << fs::absolute(ResultsDir()).string() << "\",\n";
  json << "  \"files\": {\n";
  json << "    \"bake_timings_csv\": \"results/bake_timings.csv\",\n";
  json << "    \"runtime_fps_csv\": \"results/runtime_fps.csv\",\n";
  json << "    \"ssim_results_json\": \"results/ssim_results.json\",\n";
  json << "    \"screenshots_dir\": \"results/screenshots\"\n";
  json << "  },\n";
  json << "  \"notes\": [\n";
  json << "    \"SSIM full_frame is the formal accuracy metric (entire viewport).\",\n";
  json << "    \"ROI crops in ssim_results.json are optional pixel regions only.\",\n";
  json << "    \"Run: python tools/eval_ssim.py after capturing screenshots.\"\n";
  json << "  ]\n";
  json << "}\n";

  std::ofstream out(summaryPath);
  if (out) {
    out << json.str();
    std::cout << "Evaluation summary: " << fs::absolute(summaryPath).string() << '\n';
  }
}

} // namespace app
