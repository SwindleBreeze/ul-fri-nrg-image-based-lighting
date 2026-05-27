#pragma once

#include <cstdint>
#include <vector>

#include <webgpu/webgpu_cpp.h>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include "ibl/IblBaker.h"
#include "scene/Scene.h"

namespace gfx {

class UiLayer;

class PbrRenderer {
public:
  void UploadDefaultTextures(const wgpu::Instance& instance,
                             const wgpu::Device& device,
                             const wgpu::Queue& queue);

  void Initialize(const wgpu::Instance& instance,
                  const wgpu::Device& device,
                  const wgpu::Queue& queue,
                  wgpu::TextureFormat colorFormat,
                  const ibl::IblTextures& iblTextures);

  void SetScene(scene::Scene* scene);
  void UpdateIblTextures(const ibl::IblTextures& iblTextures);
  void Resize(uint32_t width, uint32_t height);

  void Render(const wgpu::TextureView& colorView,
              const glm::vec3& cameraPos,
              const glm::mat4& view,
              const glm::mat4& proj,
              float envYawRadians,
              UiLayer* uiLayer = nullptr);

  bool IsReady() const { return ready_; }

  scene::Scene* GetScene() { return scene_; }

  wgpu::TextureView GetDefaultAlbedoView() const { return defaultAlbedoView_; }
  wgpu::TextureView GetDefaultMrView() const { return defaultMrView_; }
  wgpu::TextureView GetDefaultNormalView() const { return defaultNormalView_; }

private:
  struct GpuObject {
    scene::DrawObject object;
    wgpu::Buffer materialBuffer;
    wgpu::Buffer modelBuffer;
    wgpu::BindGroup materialBindGroup;
  };

  struct CameraUniforms {
    float viewProj[16];
    float cameraPos[4];
  };

  struct MaterialUniforms {
    float albedoMetallic[4];
    float roughnessAoFlags[4];
  };

  struct IblParamsUniform {
    float maxReflectionLod = 4.0f;
    float envYaw = 0.0f;
    float padding0 = 0.0f;
    float padding1 = 0.0f;
  };

  struct SkyParamsUniform {
    float envYaw = 0.0f;
    float padding0 = 0.0f;
    float padding1 = 0.0f;
    float padding2 = 0.0f;
  };

  wgpu::BindGroup CreateMaterialBindGroup(const scene::Material& material,
                                        const wgpu::Buffer& materialBuffer,
                                        const wgpu::Buffer& modelBuffer);
  void CreateDepthTexture();
  void CreateDefaultTextures(const wgpu::Instance& instance);

  wgpu::Device device_;
  wgpu::Queue queue_;

  wgpu::RenderPipeline pipeline_;
  wgpu::RenderPipeline skyboxPipeline_;
  wgpu::BindGroupLayout cameraBgl_;
  wgpu::BindGroupLayout iblBgl_;
  wgpu::BindGroupLayout materialBgl_;
  wgpu::BindGroupLayout skyBgl_;

  wgpu::BindGroup cameraBindGroup_;
  wgpu::BindGroup iblBindGroup_;
  wgpu::BindGroup skyboxBindGroup_;

  wgpu::Buffer cameraBuffer_;
  wgpu::Buffer skyCameraBuffer_;
  wgpu::Buffer iblParamsBuffer_;
  wgpu::Buffer skyParamsBuffer_;

  wgpu::Sampler brdfSampler_;
  wgpu::Sampler materialSampler_;

  wgpu::Texture defaultAlbedo_;
  wgpu::Texture defaultMr_;
  wgpu::Texture defaultNormal_;
  wgpu::TextureView defaultAlbedoView_;
  wgpu::TextureView defaultMrView_;
  wgpu::TextureView defaultNormalView_;

  wgpu::Texture depthTexture_;
  wgpu::TextureView depthView_;

  wgpu::Buffer skyboxVertexBuffer_;
  wgpu::Buffer skyboxIndexBuffer_;
  uint32_t skyboxIndexCount_ = 0;

  std::vector<GpuObject> gpuObjects_;
  scene::Scene* scene_ = nullptr;
  ibl::IblTextures iblTextures_{};
  float maxReflectionLod_ = 4.0f;

  uint32_t width_ = 1;
  uint32_t height_ = 1;
  bool ready_ = false;
  bool defaultTexturesUploaded_ = false;
};

} // namespace gfx
