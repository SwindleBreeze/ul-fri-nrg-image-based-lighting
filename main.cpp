#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifdef CreateWindow
#undef CreateWindow
#endif
#ifdef Success
#undef Success
#endif
#endif

#include <webgpu/webgpu_cpp.h>

#include "app/AppState.h"
#include "app/BakePipeline.h"
#include "gfx/OrbitCamera.h"
#include "gfx/PbrRenderer.h"
#include "gfx/UiLayer.h"
#include "ibl/IblBaker.h"
#include "io/HdrLoader.h"
#include "scene/Scene.h"

struct GpuContext {
  wgpu::Instance instance;
  wgpu::Adapter adapter;
  wgpu::Device device;
  wgpu::Queue queue;
};

static void Fatal(const char* message) {
  std::cerr << message << "\n";
  std::exit(1);
}

static wgpu::Instance CreateInstance() {
  static const auto kTimedWaitAny = wgpu::InstanceFeatureName::TimedWaitAny;
  wgpu::InstanceDescriptor desc{};
  desc.requiredFeatureCount = 1;
  desc.requiredFeatures = &kTimedWaitAny;
  wgpu::Instance instance = wgpu::CreateInstance(&desc);
  if (!instance) {
    Fatal("CreateInstance failed");
  }
  return instance;
}

static wgpu::Adapter RequestAdapterSync(const wgpu::Instance& instance,
                                        const wgpu::Surface& surface) {
  wgpu::RequestAdapterOptions options{};
  options.powerPreference = wgpu::PowerPreference::HighPerformance;
  options.compatibleSurface = surface;

  wgpu::Adapter adapter;
  wgpu::Future future = instance.RequestAdapter(
    &options, wgpu::CallbackMode::WaitAnyOnly,
    [&](wgpu::RequestAdapterStatus status, wgpu::Adapter a, wgpu::StringView message) {
      if (status == wgpu::RequestAdapterStatus::Success) {
        adapter = std::move(a);
      } else {
        std::cerr << "RequestAdapter failed: "
                  << std::string_view(message.data, message.length) << "\n";
      }
    });
  instance.WaitAny(future, UINT64_MAX);
  if (!adapter) {
    Fatal("RequestAdapterSync failed");
  }
  return adapter;
}

static wgpu::Device RequestDeviceSync(const wgpu::Instance& instance,
                                      const wgpu::Adapter& adapter) {
  std::vector<wgpu::FeatureName> requiredFeatures;
  if (adapter.HasFeature(wgpu::FeatureName::TextureFormatsTier2)) {
    requiredFeatures.push_back(wgpu::FeatureName::TextureFormatsTier2);
  }
  if (adapter.HasFeature(wgpu::FeatureName::ImplicitDeviceSynchronization)) {
    requiredFeatures.push_back(wgpu::FeatureName::ImplicitDeviceSynchronization);
  }
  if (adapter.HasFeature(wgpu::FeatureName::Float32Filterable)) {
    requiredFeatures.push_back(wgpu::FeatureName::Float32Filterable);
  }

  wgpu::DeviceDescriptor deviceDesc{};
  deviceDesc.requiredFeatureCount = requiredFeatures.size();
  deviceDesc.requiredFeatures =
    requiredFeatures.empty() ? nullptr : requiredFeatures.data();
  deviceDesc.SetUncapturedErrorCallback(
    [](const wgpu::Device&, wgpu::ErrorType, wgpu::StringView message) {
      std::cerr << "Uncaptured device error: "
                << std::string_view(message.data, message.length) << "\n";
    });

  wgpu::Device device;
  wgpu::Future future = adapter.RequestDevice(
    &deviceDesc, wgpu::CallbackMode::WaitAnyOnly,
    [&](wgpu::RequestDeviceStatus status, wgpu::Device d, wgpu::StringView message) {
      if (status == wgpu::RequestDeviceStatus::Success) {
        device = std::move(d);
      } else {
        std::cerr << "RequestDevice failed: "
                  << std::string_view(message.data, message.length) << "\n";
      }
    });
  instance.WaitAny(future, UINT64_MAX);
  if (!device) {
    Fatal("RequestDeviceSync failed");
  }
  return device;
}

static GLFWwindow* CreateGlfwWindow(uint32_t width, uint32_t height) {
  if (!glfwInit()) {
    return nullptr;
  }
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  return glfwCreateWindow(static_cast<int>(width), static_cast<int>(height),
                          "NRG IBL Seminar", nullptr, nullptr);
}

static wgpu::Surface CreateSurface(const wgpu::Instance& instance, GLFWwindow* window) {
#ifdef _WIN32
  wgpu::SurfaceSourceWindowsHWND hwndDesc{};
  hwndDesc.hwnd = glfwGetWin32Window(window);
  hwndDesc.hinstance = GetModuleHandle(nullptr);
  wgpu::SurfaceDescriptor surfaceDesc{};
  surfaceDesc.nextInChain = &hwndDesc;
  return instance.CreateSurface(&surfaceDesc);
#else
  (void)instance;
  (void)window;
  return {};
#endif
}

struct SurfaceConfig {
  wgpu::TextureFormat format;
  wgpu::PresentMode presentMode;
  wgpu::CompositeAlphaMode alphaMode;
};

static SurfaceConfig ChooseSurfaceConfig(const wgpu::Surface& surface,
                                         const wgpu::Adapter& adapter) {
  wgpu::SurfaceCapabilities caps{};
  surface.GetCapabilities(adapter, &caps);
  SurfaceConfig config{};
  config.format = wgpu::TextureFormat::BGRA8Unorm;
  for (size_t i = 0; i < caps.formatCount; ++i) {
    if (caps.formats[i] == wgpu::TextureFormat::BGRA8Unorm) {
      config.format = wgpu::TextureFormat::BGRA8Unorm;
      break;
    }
    if (i == 0) {
      config.format = caps.formats[0];
    }
  }
  if (caps.presentModeCount > 0) {
    config.presentMode = caps.presentModes[0];
  } else {
    config.presentMode = wgpu::PresentMode::Fifo;
  }
  if (caps.alphaModeCount > 0) {
    config.alphaMode = caps.alphaModes[0];
  } else {
    config.alphaMode = wgpu::CompositeAlphaMode::Opaque;
  }
  return config;
}

static void ConfigureSurface(const wgpu::Surface& surface,
                             const wgpu::Device& device,
                             const SurfaceConfig& surfaceConfig,
                             uint32_t width,
                             uint32_t height) {
  wgpu::SurfaceConfiguration config{};
  config.device = device;
  config.format = surfaceConfig.format;
  config.presentMode = surfaceConfig.presentMode;
  config.alphaMode = surfaceConfig.alphaMode;
  config.usage = wgpu::TextureUsage::RenderAttachment;
  config.width = width;
  config.height = height;
  surface.Configure(&config);
}

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cout << "Usage: Renderer <path-to.hdr> [path-to.gltf]\n";
    return 0;
  }

  const std::string hdrPath = argv[1];
  std::string gltfPath;
  if (argc >= 3) {
    gltfPath = argv[2];
  } else if (std::filesystem::exists("assets/DamagedHelmet.glb")) {
    gltfPath = "assets/DamagedHelmet.glb";
  }

  GpuContext gpu{};
  gpu.instance = CreateInstance();

  const uint32_t windowWidth = 1280;
  const uint32_t windowHeight = 720;
  GLFWwindow* window = CreateGlfwWindow(windowWidth, windowHeight);
  if (!window) {
    return 1;
  }

  wgpu::Surface surface = CreateSurface(gpu.instance, window);
  if (!surface) {
    glfwDestroyWindow(window);
    glfwTerminate();
    return 1;
  }

  gpu.adapter = RequestAdapterSync(gpu.instance, surface);
  gpu.device = RequestDeviceSync(gpu.instance, gpu.adapter);
  gpu.queue = gpu.device.GetQueue();

  SurfaceConfig surfaceConfig = ChooseSurfaceConfig(surface, gpu.adapter);
  ConfigureSurface(surface, gpu.device, surfaceConfig, windowWidth, windowHeight);

  io::HdrImage hdrImage = io::LoadHdrImageRGBA32F(hdrPath);
  if (hdrImage.pixels.empty()) {
    return 1;
  }

  io::HdrTexture hdrTexture = io::CreateEquirectHdrTexture(gpu.device, gpu.queue, hdrImage);
  if (!hdrTexture.texture) {
    return 1;
  }

  gfx::PbrRenderer renderer;
  renderer.UploadDefaultTextures(gpu.instance, gpu.device, gpu.queue);
  app::WaitForQueue(gpu.instance, gpu.queue);

  app::AppState appState{};
  ibl::DestroyIblTextures(ibl::IblTextures{});
  ibl::IblTextures iblTextures = app::RunIblBake(gpu.instance, gpu.device, gpu.queue, hdrTexture,
                                               appState.bakeSettings, appState.bakeTimings);
  if (!ibl::IblTexturesReady(iblTextures)) {
    Fatal("IBL bake finished but textures are missing.");
  }

  app::WaitForQueue(gpu.instance, gpu.queue);
  gpu.instance.ProcessEvents();
  glfwPollEvents();
  renderer.Initialize(gpu.instance, gpu.device, gpu.queue, surfaceConfig.format, iblTextures);
  if (!renderer.IsReady()) {
    Fatal("PbrRenderer initialization failed (see errors above).");
  }
  renderer.Resize(windowWidth, windowHeight);
  scene::Scene scene = scene::BuildDefaultScene(gpu.device, gpu.queue, gltfPath,
                                                renderer.GetDefaultAlbedoView(),
                                                renderer.GetDefaultMrView(),
                                                renderer.GetDefaultNormalView());
  renderer.SetScene(&scene);

  gfx::UiLayer ui;
  if (!ui.Init(gpu.device, gpu.queue, surfaceConfig.format, window)) {
    Fatal("ImGui initialization failed.");
  }

  gfx::OrbitCamera camera;
  camera.Attach(window); // after ImGui init; forwards input to ImGui then orbit

  std::vector<double> frameTimes;
  frameTimes.reserve(128);

  uint32_t currentWidth = windowWidth;
  uint32_t currentHeight = windowHeight;

  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();

    if (appState.rebakeRequested && !appState.rebaking) {
      appState.rebaking = true;
      appState.rebakeRequested = false;
      ibl::DestroyIblTextures(iblTextures);
      iblTextures = app::RunIblBake(gpu.instance, gpu.device, gpu.queue, hdrTexture,
                                    appState.bakeSettings, appState.bakeTimings);
      renderer.UpdateIblTextures(iblTextures);
      appState.rebaking = false;
    }

    int fbWidth = 0;
    int fbHeight = 0;
    glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
    if (fbWidth <= 0 || fbHeight <= 0) {
      continue;
    }

    const uint32_t newWidth = static_cast<uint32_t>(fbWidth);
    const uint32_t newHeight = static_cast<uint32_t>(fbHeight);
    if (newWidth != currentWidth || newHeight != currentHeight) {
      currentWidth = newWidth;
      currentHeight = newHeight;
      ConfigureSurface(surface, gpu.device, surfaceConfig, currentWidth, currentHeight);
      renderer.Resize(currentWidth, currentHeight);
    }

    const auto frameStart = std::chrono::steady_clock::now();

    ui.BeginFrame();
    ui.Build(appState, scene);

    wgpu::SurfaceTexture surfaceTexture{};
    surface.GetCurrentTexture(&surfaceTexture);

    const bool surfaceTextureReady =
      (surfaceTexture.status == wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal ||
       surfaceTexture.status == wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal) &&
      surfaceTexture.texture;

    if (!surfaceTextureReady) {
      if (surfaceTexture.status == wgpu::SurfaceGetCurrentTextureStatus::Outdated) {
        ConfigureSurface(surface, gpu.device, surfaceConfig, currentWidth, currentHeight);
      }
      continue;
    }

    wgpu::TextureView colorView = surfaceTexture.texture.CreateView();
    const float aspect = static_cast<float>(currentWidth) / static_cast<float>(currentHeight);
    const float envYaw = glm::radians(appState.envYawDegrees);

    renderer.Render(colorView, camera.GetPosition(), camera.GetViewMatrix(),
                    camera.GetProjection(aspect), envYaw, &ui);

    surface.Present();
    gpu.instance.ProcessEvents();

    const double frameMs =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frameStart)
        .count();
    frameTimes.push_back(frameMs);
    if (frameTimes.size() > 120) {
      frameTimes.erase(frameTimes.begin());
    }
    double sum = 0.0;
    double minMs = 1e9;
    for (double t : frameTimes) {
      sum += t;
      minMs = std::min(minMs, t);
    }
    appState.fpsAvg = static_cast<float>(1000.0 / (sum / frameTimes.size()));
    appState.fpsMin = static_cast<float>(1000.0 / minMs);
  }

  ui.Shutdown();
  renderer.SetScene(nullptr);
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
