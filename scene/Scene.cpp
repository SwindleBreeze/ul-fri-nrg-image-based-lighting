#include "scene/Scene.h"

#include <iostream>

#include <glm/gtc/matrix_transform.hpp>

#include "io/GltfLoader.h"
#include "mesh/SphereMesh.h"

namespace scene {

namespace {

static void UploadMeshObject(const wgpu::Device& device,
                             const wgpu::Queue& queue,
                             const mesh::MeshData& mesh,
                             const glm::mat4& model,
                             const Material& material,
                             const char* name,
                             Scene& scene) {
  DrawObject object{};
  object.name = name;
  object.model = model;
  object.material = material;

  wgpu::BufferDescriptor vbDesc{};
  vbDesc.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;
  vbDesc.size = mesh.vertices.size() * sizeof(mesh::Vertex);
  object.vertexBuffer = device.CreateBuffer(&vbDesc);
  queue.WriteBuffer(object.vertexBuffer, 0, mesh.vertices.data(), vbDesc.size);

  wgpu::BufferDescriptor ibDesc{};
  ibDesc.usage = wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst;
  ibDesc.size = mesh.indices.size() * sizeof(uint32_t);
  object.indexBuffer = device.CreateBuffer(&ibDesc);
  queue.WriteBuffer(object.indexBuffer, 0, mesh.indices.data(), ibDesc.size);
  object.indexCount = static_cast<uint32_t>(mesh.indices.size());

  scene.objects.push_back(std::move(object));
}

} // namespace

Scene BuildDefaultScene(const wgpu::Device& device,
                        const wgpu::Queue& queue,
                        const std::string& gltfPath,
                        wgpu::TextureView defaultAlbedo,
                        wgpu::TextureView defaultMr,
                        wgpu::TextureView defaultNormal) {
  Scene scene;

  if (!gltfPath.empty()) {
    io::GltfLoadResult gltf =
      io::LoadGltfScene(device, queue, gltfPath, defaultAlbedo, defaultMr, defaultNormal);
    if (gltf.success) {
      for (scene::DrawObject& object : gltf.objects) {
        scene.objects.push_back(std::move(object));
      }
      for (wgpu::Texture& tex : gltf.ownedTextures) {
        scene.ownedTextures.push_back(std::move(tex));
      }
    } else {
      std::cerr << "glTF load failed, continuing with debug spheres only.\n";
    }
  }

  constexpr float kSphereRadius = 1.0f;
  constexpr float kSphereCenterY = 0.0f;

  Material dielectric{};
  dielectric.albedo = glm::vec3(0.2f, 0.45f, 0.7f);
  dielectric.metallic = 0.0f;
  dielectric.roughness = 0.25f;
  dielectric.albedoView = defaultAlbedo;
  dielectric.metallicRoughnessView = defaultMr;
  dielectric.normalView = defaultNormal;

  Material metal{};
  metal.albedo = glm::vec3(0.95f, 0.95f, 0.95f);
  metal.metallic = 1.0f;
  metal.roughness = 0.12f;
  metal.albedoView = defaultAlbedo;
  metal.metallicRoughnessView = defaultMr;
  metal.normalView = defaultNormal;

  const glm::vec3 dielectricCenter(-2.5f, kSphereCenterY, 0.0f);
  const glm::vec3 metalCenter(2.5f, kSphereCenterY, 0.0f);

  UploadMeshObject(device, queue, mesh::CreateSphereMesh(48, 48),
                   glm::translate(glm::mat4(1.0f), dielectricCenter), dielectric,
                   "dielectric_sphere", scene);

  UploadMeshObject(device, queue, mesh::CreateSphereMesh(48, 48),
                   glm::translate(glm::mat4(1.0f), metalCenter), metal, "metal_sphere", scene);

  return scene;
}

} // namespace scene
