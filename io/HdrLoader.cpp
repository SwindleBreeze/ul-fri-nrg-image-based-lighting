#include "io/HdrLoader.h"

#include <cstring>
#include <iostream>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace io {

// WebGPU requires bytesPerRow to be a multiple of 256 for texture uploads.
// This helper rounds up to the next 256-byte boundary.
static uint32_t AlignBytesPerRow(uint32_t bytesPerRow) {
  const uint32_t kAlignment = 256u; // WebGPU row alignment requirement.
  return (bytesPerRow + (kAlignment - 1u)) & ~(kAlignment - 1u);
}

HdrImage LoadHdrImageRGBA32F(const std::string& path) {
  // stb_image flips images by default for OpenGL; we keep the explicit flip here
  // because many HDR assets are stored with the origin at the top-left.
  stbi_set_flip_vertically_on_load(true);

  int width = 0;      // stb_image outputs width as an int.
  int height = 0;     // stb_image outputs height as an int.
  int channels = 0;   // Number of channels in the source file (ignored here).

  // Force 4 channels (RGBA) to match WebGPU's HDR texture formats.
  float* data = stbi_loadf(path.c_str(), &width, &height, &channels, 4);
  if (!data) {
    std::cerr << "Failed to load HDR image: " << path << "\n";
    return {}; // Return an empty image so callers can detect failure.
  }

  const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;

  HdrImage image;
  image.width = static_cast<uint32_t>(width);
  image.height = static_cast<uint32_t>(height);
  image.pixels.assign(data, data + pixelCount); // Copy pixels into a safe C++ container.

  stbi_image_free(data); // Free stb_image's internal buffer.

  return image; // Return the loaded image.
}

HdrTexture CreateEquirectHdrTexture(const wgpu::Device& device,
                                    const wgpu::Queue& queue,
                                    const HdrImage& image) {
  if (image.pixels.empty() || image.width == 0 || image.height == 0) {
    std::cerr << "CreateEquirectHdrTexture: empty HDR image." << "\n";
    return {};
  }

  // Create the HDR texture as RGBA32Float as requested in the spec.
  wgpu::TextureDescriptor desc{};
  desc.size = { image.width, image.height, 1 }; // 2D texture with one layer.
  desc.mipLevelCount = 1;                       // No mipmaps for the source equirect map.
  desc.sampleCount = 1;                         // Single-sampled texture.
  desc.dimension = wgpu::TextureDimension::e2D; // 2D texture (not a cube).
  desc.format = wgpu::TextureFormat::RGBA32Float; // Full-precision HDR source.
  desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst; // Sample + upload.

  wgpu::Texture texture = device.CreateTexture(&desc); // Allocate the GPU texture.

  // Compute the tightly packed row size in bytes.
  const uint32_t bytesPerPixel = 4u * static_cast<uint32_t>(sizeof(float));
  const uint32_t unalignedBytesPerRow = image.width * bytesPerPixel;
  const uint32_t alignedBytesPerRow = AlignBytesPerRow(unalignedBytesPerRow);

  // If the row size is already aligned, we can upload directly.
  // Otherwise, we create a padded buffer so each row meets the 256-byte alignment rule.
  std::vector<uint8_t> uploadData;
  const uint8_t* srcBytes = reinterpret_cast<const uint8_t*>(image.pixels.data());

  if (alignedBytesPerRow == unalignedBytesPerRow) {
    uploadData.assign(srcBytes, srcBytes + (unalignedBytesPerRow * image.height));
  } else {
    uploadData.resize(static_cast<size_t>(alignedBytesPerRow) * image.height);
    for (uint32_t y = 0; y < image.height; ++y) {
      const uint8_t* srcRow = srcBytes + static_cast<size_t>(y) * unalignedBytesPerRow;
      uint8_t* dstRow = uploadData.data() + static_cast<size_t>(y) * alignedBytesPerRow;
      std::memcpy(dstRow, srcRow, unalignedBytesPerRow); // Copy only valid pixels per row.
    }
  }

  // Describe the destination subresource (mip 0, layer 0).
  wgpu::TexelCopyTextureInfo dst{};
  dst.texture = texture;
  dst.mipLevel = 0;
  dst.origin = { 0, 0, 0 };
  dst.aspect = wgpu::TextureAspect::All;

  // Describe how the CPU buffer is laid out in memory.
  wgpu::TexelCopyBufferLayout layout{};
  layout.offset = 0;
  layout.bytesPerRow = alignedBytesPerRow; // Must be a multiple of 256.
  layout.rowsPerImage = image.height;

  // Describe the size of the copy in texels.
  wgpu::Extent3D extent{ image.width, image.height, 1 };

  // Upload the data into the GPU texture.
  queue.WriteTexture(&dst, uploadData.data(), uploadData.size(), &layout, &extent);

  // Create a default 2D view for sampling in shaders.
  wgpu::TextureView view = texture.CreateView();

  // Create a linear clamp sampler for equirectangular lookups.
  wgpu::SamplerDescriptor samplerDesc{};
  samplerDesc.addressModeU = wgpu::AddressMode::ClampToEdge;
  samplerDesc.addressModeV = wgpu::AddressMode::ClampToEdge;
  samplerDesc.addressModeW = wgpu::AddressMode::ClampToEdge;
  samplerDesc.magFilter = wgpu::FilterMode::Linear;
  samplerDesc.minFilter = wgpu::FilterMode::Linear;
  samplerDesc.mipmapFilter = wgpu::MipmapFilterMode::Linear;

  wgpu::Sampler sampler = device.CreateSampler(&samplerDesc);

  HdrTexture result;
  result.texture = texture;
  result.view = view;
  result.sampler = sampler;
  result.width = image.width;
  result.height = image.height;

  return result; // Return the fully initialized GPU texture bundle.
}

} // namespace io
