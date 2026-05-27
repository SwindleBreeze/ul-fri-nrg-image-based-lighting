#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <webgpu/webgpu_cpp.h>

namespace io {

// CPU-side representation of an HDR image loaded from disk.
// Pixels are stored as 32-bit floats in RGBA order (4 channels).
struct HdrImage {
  uint32_t width = 0;               // Image width in pixels.
  uint32_t height = 0;              // Image height in pixels.
  std::vector<float> pixels;        // Interleaved RGBA float pixels.
};

// GPU-side representation of the equirectangular HDR texture.
// This bundles the texture, its default view, and the sampler.
struct HdrTexture {
  wgpu::Texture texture;            // The GPU texture (RGBA32Float).
  wgpu::TextureView view;           // A 2D view for sampling.
  wgpu::Sampler sampler;            // Linear clamp sampler for sampling HDR.
  uint32_t width = 0;               // Cached width for convenience.
  uint32_t height = 0;              // Cached height for convenience.
};

// Load an HDR image as RGBA32F (4 channels) using stb_image.
// This enforces 4 channels to match WebGPU's RGBA-only HDR formats.
HdrImage LoadHdrImageRGBA32F(const std::string& path);

// Upload the HDR image to the GPU as an RGBA32Float texture.
// The returned texture is created with TextureBinding | CopyDst usage.
HdrTexture CreateEquirectHdrTexture(const wgpu::Device& device,
                                    const wgpu::Queue& queue,
                                    const HdrImage& image);

} // namespace io
