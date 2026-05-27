#include "gfx/UiLayer.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_wgpu.h>

#include <algorithm>
#include "ibl/IblBaker.h"

namespace gfx {

bool UiLayer::Init(const wgpu::Device& device,
                   const wgpu::Queue& queue,
                   wgpu::TextureFormat surfaceFormat,
                   GLFWwindow* window) {
  (void)queue;
  device_ = device;
  window_ = window;

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

  ImGui::StyleColorsDark();
  // OrbitCamera::Attach installs GLFW callbacks and forwards events to ImGui first.
  ImGui_ImplGlfw_InitForOther(window, false);

  ImGui_ImplWGPU_InitInfo initInfo{};
  initInfo.Device = device.Get();
  initInfo.NumFramesInFlight = 3;
  initInfo.RenderTargetFormat = static_cast<WGPUTextureFormat>(surfaceFormat);
  initInfo.DepthStencilFormat = WGPUTextureFormat_Undefined;

  if (!ImGui_ImplWGPU_Init(&initInfo)) {
    return false;
  }

  initialized_ = true;
  return true;
}

void UiLayer::Shutdown() {
  if (!initialized_) {
    return;
  }
  ImGui_ImplWGPU_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  initialized_ = false;
  window_ = nullptr;
}

void UiLayer::BeginFrame() {
  if (!initialized_) {
    return;
  }
  ImGui_ImplGlfw_NewFrame();
  ImGui_ImplWGPU_NewFrame();
  ImGui::NewFrame();
}

void UiLayer::Build(app::AppState& state, scene::Scene& scene) {
  if (!initialized_) {
    return;
  }

  ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(320, 0), ImGuiCond_FirstUseEver);
  ImGui::Begin("IBL controls");

  ImGui::Text("FPS: %.0f (min %.0f)", state.fpsAvg, state.fpsMin);
  ImGui::Text("Last bake: %.0f ms", state.bakeTimings.totalMs);
  if (state.rebaking) {
    ImGui::TextColored(ImVec4(1, 0.8f, 0.2f, 1), "Rebaking...");
  }

  ImGui::Separator();
  ImGui::TextUnformatted("IBL quality");
  const char* presets[] = { "Low", "Medium", "High" };
  int presetIndex = 1;
  if (state.qualityPreset == ibl::IblQualityPreset::Low) {
    presetIndex = 0;
  } else if (state.qualityPreset == ibl::IblQualityPreset::High) {
    presetIndex = 2;
  }
  if (ImGui::Combo("Preset", &presetIndex, presets, 3)) {
    switch (presetIndex) {
      case 0:
        state.qualityPreset = ibl::IblQualityPreset::Low;
        break;
      case 2:
        state.qualityPreset = ibl::IblQualityPreset::High;
        break;
      default:
        state.qualityPreset = ibl::IblQualityPreset::Medium;
        break;
    }
    state.bakeSettings = ibl::SettingsForPreset(state.qualityPreset);
  }
  if (ImGui::Button("Rebake IBL") || ImGui::IsKeyPressed(ImGuiKey_R)) {
    state.rebakeRequested = true;
  }

  ImGui::Separator();
  ImGui::SliderFloat("Env yaw (deg)", &state.envYawDegrees, -180.0f, 180.0f);

  ImGui::Separator();
  ImGui::TextUnformatted("Selected object");
  if (!scene.objects.empty()) {
    state.selectedObjectIndex =
      std::clamp(state.selectedObjectIndex, 0, static_cast<int>(scene.objects.size()) - 1);

    if (ImGui::BeginCombo("Object", scene.objects[static_cast<size_t>(state.selectedObjectIndex)].name.c_str())) {
      for (int i = 0; i < static_cast<int>(scene.objects.size()); ++i) {
        const bool selected = i == state.selectedObjectIndex;
        if (ImGui::Selectable(scene.objects[static_cast<size_t>(i)].name.c_str(), selected)) {
          state.selectedObjectIndex = i;
        }
        if (selected) {
          ImGui::SetItemDefaultFocus();
        }
      }
      ImGui::EndCombo();
    }

    scene::Material& mat = scene.objects[static_cast<size_t>(state.selectedObjectIndex)].material;
    ImGui::SliderFloat("Metallic", &mat.metallic, 0.0f, 1.0f);
    ImGui::SliderFloat("Roughness", &mat.roughness, 0.04f, 1.0f);
    ImGui::ColorEdit3("Albedo", &mat.albedo.x);
  } else {
    ImGui::TextUnformatted("(no objects)");
  }

  ImGui::Separator();
  ImGui::TextWrapped(
    "Camera: LMB drag orbit, scroll zoom. IBL lights objects from the HDR only; "
    "spheres are not mirrored in the floor (no screen-space / planar reflections).");

  ImGui::End();
  ImGui::Render();
}

void UiLayer::RenderDrawData(wgpu::RenderPassEncoder& pass) {
  if (!initialized_) {
    return;
  }
  if (ImDrawData* drawData = ImGui::GetDrawData()) {
    ImGui_ImplWGPU_RenderDrawData(drawData, pass.Get());
  }
}

bool UiLayer::WantsCaptureMouse() const {
  return initialized_ && ImGui::GetIO().WantCaptureMouse;
}

bool UiLayer::WantsCaptureKeyboard() const {
  return initialized_ && ImGui::GetIO().WantCaptureKeyboard;
}

} // namespace gfx
