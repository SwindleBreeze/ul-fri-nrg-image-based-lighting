#pragma once

#include <webgpu/webgpu_cpp.h>

struct GLFWwindow;

#include "app/AppState.h"
#include "scene/Scene.h"

namespace gfx {

class OrbitCamera;

class UiLayer {
public:
  bool Init(const wgpu::Device& device,
            const wgpu::Queue& queue,
            wgpu::TextureFormat surfaceFormat,
            GLFWwindow* window);
  void Shutdown();

  void BeginFrame();
  void Build(app::AppState& state, scene::Scene& scene, OrbitCamera& camera);
  void RenderDrawData(wgpu::RenderPassEncoder& pass);

  bool WantsCaptureMouse() const;
  bool WantsCaptureKeyboard() const;

private:
  wgpu::Device device_;
  GLFWwindow* window_ = nullptr;
  bool initialized_ = false;
};

} // namespace gfx
