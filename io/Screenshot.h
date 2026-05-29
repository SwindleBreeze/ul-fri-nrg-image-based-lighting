#pragma once

#include <cstdint>
#include <string>

#include <webgpu/webgpu_cpp.h>

namespace io {

// Read back a BGRA8 swapchain texture and write an 8-bit RGB PNG.
bool SaveTextureToPng(const wgpu::Instance& instance,
                       const wgpu::Device& device,
                       const wgpu::Queue& queue,
                       wgpu::Texture texture,
                       wgpu::TextureFormat format,
                       uint32_t width,
                       uint32_t height,
                       const std::string& path);

} // namespace io
