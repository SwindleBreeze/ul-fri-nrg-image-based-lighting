#include "io/GltfLoader.h"

#include <cmath>
#include <iostream>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#define TINYGLTF_IMPLEMENTATION
#include <stb_image.h>
#include <tiny_gltf.h>

namespace io {

namespace {

static wgpu::Texture UploadRgba8(const wgpu::Device& device,
                                 const wgpu::Queue& queue,
                                 const unsigned char* pixels,
                                 uint32_t width,
                                 uint32_t height) {
  wgpu::TextureDescriptor desc{};
  desc.size = { width, height, 1 };
  desc.format = wgpu::TextureFormat::RGBA8Unorm;
  desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
  wgpu::Texture texture = device.CreateTexture(&desc);

  wgpu::TexelCopyTextureInfo dst{};
  dst.texture = texture;
  dst.mipLevel = 0;
  dst.origin = { 0, 0, 0 };
  dst.aspect = wgpu::TextureAspect::All;

  wgpu::TexelCopyBufferLayout layout{};
  layout.offset = 0;
  layout.bytesPerRow = width * 4;
  layout.rowsPerImage = height;

  wgpu::Extent3D extent{ width, height, 1 };
  queue.WriteTexture(&dst, pixels, static_cast<size_t>(width) * height * 4, &layout, &extent);
  return texture;
}

static glm::vec3 ReadVec3(const std::vector<double>& v) {
  if (v.size() < 3) {
    return glm::vec3(0.0f);
  }
  return glm::vec3(static_cast<float>(v[0]), static_cast<float>(v[1]), static_cast<float>(v[2]));
}

static glm::mat4 ComputeNodeLocalTransform(const tinygltf::Node& node) {
  glm::mat4 nodeTransform(1.0f);
  if (node.matrix.size() == 16) {
    for (int i = 0; i < 16; ++i) {
      nodeTransform[i / 4][i % 4] = static_cast<float>(node.matrix[i]);
    }
    return nodeTransform;
  }

  const glm::vec3 t = ReadVec3(node.translation);
  const glm::vec3 s = node.scale.empty() ? glm::vec3(1.0f) : ReadVec3(node.scale);
  const glm::quat q = node.rotation.size() == 4
                        ? glm::quat(static_cast<float>(node.rotation[3]),
                                    static_cast<float>(node.rotation[0]),
                                    static_cast<float>(node.rotation[1]),
                                    static_cast<float>(node.rotation[2]))
                        : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
  return glm::translate(glm::mat4(1.0f), t) * glm::mat4_cast(q) * glm::scale(glm::mat4(1.0f), s);
}

static void UploadMesh(const wgpu::Device& device,
                       const wgpu::Queue& queue,
                       const std::vector<mesh::Vertex>& vertices,
                       const std::vector<uint32_t>& indices,
                       scene::DrawObject& out) {
  wgpu::BufferDescriptor vbDesc{};
  vbDesc.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;
  vbDesc.size = vertices.size() * sizeof(mesh::Vertex);
  out.vertexBuffer = device.CreateBuffer(&vbDesc);
  queue.WriteBuffer(out.vertexBuffer, 0, vertices.data(), vbDesc.size);

  wgpu::BufferDescriptor ibDesc{};
  ibDesc.usage = wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst;
  ibDesc.size = indices.size() * sizeof(uint32_t);
  out.indexBuffer = device.CreateBuffer(&ibDesc);
  queue.WriteBuffer(out.indexBuffer, 0, indices.data(), ibDesc.size);
  out.indexCount = static_cast<uint32_t>(indices.size());
}

} // namespace

GltfLoadResult LoadGltfScene(const wgpu::Device& device,
                               const wgpu::Queue& queue,
                               const std::string& path,
                               wgpu::TextureView defaultAlbedo,
                               wgpu::TextureView defaultMr,
                               wgpu::TextureView defaultNormal) {
  GltfLoadResult result{};

  tinygltf::Model model;
  tinygltf::TinyGLTF loader;
  std::string err;
  std::string warn;

  const bool ok = loader.LoadBinaryFromFile(&model, &err, &warn, path);
  if (!warn.empty()) {
    std::cerr << "glTF warn: " << warn << "\n";
  }
  if (!ok) {
    std::cerr << "glTF load failed: " << err << "\n";
    return result;
  }

  auto loadImageView = [&](int imageIndex) -> wgpu::TextureView {
    if (imageIndex < 0 || imageIndex >= static_cast<int>(model.images.size())) {
      return defaultAlbedo;
    }
    const tinygltf::Image& image = model.images[imageIndex];
    if (image.image.empty() || image.width == 0 || image.height == 0) {
      return defaultAlbedo;
    }
    wgpu::Texture tex = UploadRgba8(device, queue, image.image.data(),
                                    static_cast<uint32_t>(image.width),
                                    static_cast<uint32_t>(image.height));
    result.ownedTextures.push_back(tex);
    return tex.CreateView();
  };

  if (model.scenes.empty() || model.meshes.empty()) {
    return result;
  }

  const int sceneIndex = model.defaultScene >= 0 ? model.defaultScene : 0;
  const tinygltf::Scene& gltfScene = model.scenes[sceneIndex];

  glm::vec3 boundsMin(1e9f);
  glm::vec3 boundsMax(-1e9f);

  std::vector<std::pair<int, glm::mat4>> nodeStack;
  for (int rootNode : gltfScene.nodes) {
    nodeStack.emplace_back(rootNode, glm::mat4(1.0f));
  }

  while (!nodeStack.empty()) {
    const auto [nodeIndex, parentTransform] = nodeStack.back();
    nodeStack.pop_back();
    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(model.nodes.size())) {
      continue;
    }
    const tinygltf::Node& node = model.nodes[nodeIndex];
    const glm::mat4 nodeTransform = parentTransform * ComputeNodeLocalTransform(node);

    for (int childIndex : node.children) {
      nodeStack.emplace_back(childIndex, nodeTransform);
    }

    if (node.mesh < 0 || node.mesh >= static_cast<int>(model.meshes.size())) {
      continue;
    }

    const tinygltf::Mesh& gltfMesh = model.meshes[node.mesh];

    for (const tinygltf::Primitive& primitive : gltfMesh.primitives) {
      if (primitive.mode != TINYGLTF_MODE_TRIANGLES) {
        continue;
      }

      const auto posIt = primitive.attributes.find("POSITION");
      if (posIt == primitive.attributes.end()) {
        continue;
      }
      if (primitive.indices < 0) {
        continue;
      }

      const tinygltf::Accessor& posAccessor = model.accessors[posIt->second];
      const tinygltf::BufferView& posView = model.bufferViews[posAccessor.bufferView];
      const tinygltf::Buffer& posBuffer = model.buffers[posView.buffer];
      const float* positions = reinterpret_cast<const float*>(
        &posBuffer.data[posAccessor.byteOffset + posView.byteOffset]);

      const tinygltf::Accessor* normAccessor = nullptr;
      const float* normals = nullptr;
      const auto normIt = primitive.attributes.find("NORMAL");
      if (normIt != primitive.attributes.end()) {
        normAccessor = &model.accessors[normIt->second];
        const tinygltf::BufferView& normView = model.bufferViews[normAccessor->bufferView];
        const tinygltf::Buffer& normBuffer = model.buffers[normView.buffer];
        normals = reinterpret_cast<const float*>(
          &normBuffer.data[normAccessor->byteOffset + normView.byteOffset]);
      }

      const tinygltf::Accessor* uvAccessor = nullptr;
      const float* uvs = nullptr;
      const auto uvIt = primitive.attributes.find("TEXCOORD_0");
      if (uvIt != primitive.attributes.end()) {
        uvAccessor = &model.accessors[uvIt->second];
        const tinygltf::BufferView& uvView = model.bufferViews[uvAccessor->bufferView];
        const tinygltf::Buffer& uvBuffer = model.buffers[uvView.buffer];
        uvs = reinterpret_cast<const float*>(
          &uvBuffer.data[uvAccessor->byteOffset + uvView.byteOffset]);
      }

      const tinygltf::Accessor* tanAccessor = nullptr;
      const float* tangents = nullptr;
      const auto tanIt = primitive.attributes.find("TANGENT");
      if (tanIt != primitive.attributes.end()) {
        tanAccessor = &model.accessors[tanIt->second];
        const tinygltf::BufferView& tanView = model.bufferViews[tanAccessor->bufferView];
        const tinygltf::Buffer& tanBuffer = model.buffers[tanView.buffer];
        tangents = reinterpret_cast<const float*>(
          &tanBuffer.data[tanAccessor->byteOffset + tanView.byteOffset]);
      }

      const tinygltf::Accessor& indexAccessor = model.accessors[primitive.indices];
      const tinygltf::BufferView& indexView = model.bufferViews[indexAccessor.bufferView];
      const tinygltf::Buffer& indexBuffer = model.buffers[indexView.buffer];
      const uint8_t* indexData =
        &indexBuffer.data[indexAccessor.byteOffset + indexView.byteOffset];

      std::vector<mesh::Vertex> vertices(posAccessor.count);
      for (size_t i = 0; i < posAccessor.count; ++i) {
        glm::vec3 pos(positions[i * 3 + 0], positions[i * 3 + 1], positions[i * 3 + 2]);
        glm::vec3 nrm = normals ? glm::vec3(normals[i * 3 + 0], normals[i * 3 + 1], normals[i * 3 + 2])
                                : glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec2 uv = uvs ? glm::vec2(uvs[i * 2 + 0], uvs[i * 2 + 1]) : glm::vec2(0.0f);
        glm::vec4 tangent = tangents ? glm::vec4(tangents[i * 4 + 0], tangents[i * 4 + 1],
                                                 tangents[i * 4 + 2], tangents[i * 4 + 3])
                                     : glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
        vertices[i] = { pos, nrm, uv, tangent };

        const glm::vec3 wp = glm::vec3(nodeTransform * glm::vec4(pos, 1.0f));
        boundsMin = glm::min(boundsMin, wp);
        boundsMax = glm::max(boundsMax, wp);
      }

      std::vector<uint32_t> indices(indexAccessor.count);
      for (size_t i = 0; i < indexAccessor.count; ++i) {
        uint32_t index = 0;
        switch (indexAccessor.componentType) {
          case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
            index = reinterpret_cast<const uint16_t*>(indexData)[i];
            break;
          case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
            index = reinterpret_cast<const uint32_t*>(indexData)[i];
            break;
          case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
            index = indexData[i];
            break;
          default:
            break;
        }
        indices[i] = index;
      }

      scene::DrawObject object{};
      object.name = gltfMesh.name.empty() ? "gltf_mesh" : gltfMesh.name;
      object.model = nodeTransform;
      UploadMesh(device, queue, vertices, indices, object);

      if (primitive.material >= 0 &&
          primitive.material < static_cast<int>(model.materials.size())) {
        const tinygltf::Material& mat = model.materials[primitive.material];
        const auto& pbr = mat.pbrMetallicRoughness;
        object.material.albedo = ReadVec3(pbr.baseColorFactor);
        object.material.metallic = static_cast<float>(pbr.metallicFactor);
        object.material.roughness = static_cast<float>(pbr.roughnessFactor);
        object.material.ao = 1.0f;

        if (pbr.baseColorTexture.index >= 0) {
          object.material.useAlbedoMap = true;
          object.material.albedoView = loadImageView(model.textures[pbr.baseColorTexture.index].source);
        } else {
          object.material.albedoView = defaultAlbedo;
        }

        if (pbr.metallicRoughnessTexture.index >= 0) {
          object.material.useMetallicRoughnessMap = true;
          object.material.metallicRoughnessView =
            loadImageView(model.textures[pbr.metallicRoughnessTexture.index].source);
        } else {
          object.material.metallicRoughnessView = defaultMr;
        }

        if (mat.normalTexture.index >= 0) {
          object.material.useNormalMap = true;
          object.material.normalView =
            loadImageView(model.textures[mat.normalTexture.index].source);
        } else {
          object.material.normalView = defaultNormal;
        }
      } else {
        object.material.albedoView = defaultAlbedo;
        object.material.metallicRoughnessView = defaultMr;
        object.material.normalView = defaultNormal;
      }

      result.objects.push_back(std::move(object));
    }
  }

  if (!result.objects.empty()) {
    const glm::vec3 center = 0.5f * (boundsMin + boundsMax);
    const glm::vec3 extent = boundsMax - boundsMin;
    const float maxExtent = std::max(extent.x, std::max(extent.y, extent.z));
    const float scale = maxExtent > 0.0f ? 1.5f / maxExtent : 1.0f;
    const glm::mat4 normalizeTransform =
      glm::scale(glm::translate(glm::mat4(1.0f), -center), glm::vec3(scale));
    const glm::mat4 flipY = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, -1.0f, 1.0f));
    for (scene::DrawObject& object : result.objects) {
      object.model = flipY * normalizeTransform * object.model;
    }
    result.success = true;
  } else {
    std::cerr << "glTF parsed but produced no drawable triangle primitives (possibly unsupported compression or node layout).\n";
  }

  return result;
}

} // namespace io
