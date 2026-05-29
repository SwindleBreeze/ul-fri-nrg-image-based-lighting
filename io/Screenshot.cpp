#include "io/Screenshot.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <filesystem>
#include <iostream>
#include <vector>

#include "utils/QueueSync.h"

namespace io {

namespace {

uint32_t Align256(uint32_t value) {
  return (value + 255u) & ~255u;
}

} // namespace

bool SaveTextureToPng(const wgpu::Instance& instance,
                      const wgpu::Device& device,
                      const wgpu::Queue& queue,
                      wgpu::Texture texture,
                      wgpu::TextureFormat format,
                      uint32_t width,
                      uint32_t height,
                      const std::string& path) {
  if (!texture || width == 0 || height == 0) {
    return false;
  }

  if (format != wgpu::TextureFormat::BGRA8Unorm &&
      format != wgpu::TextureFormat::RGBA8Unorm) {
    std::cerr << "Screenshot: unsupported format " << static_cast<int>(format) << "\n";
    return false;
  }

  const uint32_t bytesPerPixel = 4;
  const uint32_t bytesPerRow = Align256(width * bytesPerPixel);
  const uint64_t bufferSize = static_cast<uint64_t>(bytesPerRow) * height;

  wgpu::BufferDescriptor bufferDesc{};
  bufferDesc.size = bufferSize;
  bufferDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
  wgpu::Buffer readbackBuffer = device.CreateBuffer(&bufferDesc);
  if (!readbackBuffer) {
    return false;
  }

  wgpu::CommandEncoder encoder = device.CreateCommandEncoder();

  wgpu::TexelCopyTextureInfo src{};
  src.texture = texture;
  src.mipLevel = 0;
  src.origin = { 0, 0, 0 };
  src.aspect = wgpu::TextureAspect::All;

  wgpu::TexelCopyBufferInfo dst{};
  dst.buffer = readbackBuffer;
  dst.layout.offset = 0;
  dst.layout.bytesPerRow = bytesPerRow;
  dst.layout.rowsPerImage = height;

  wgpu::Extent3D extent{ width, height, 1 };
  encoder.CopyTextureToBuffer(&src, &dst, &extent);
  queue.Submit(1, &encoder.Finish());

  utils::WaitForQueue(instance, queue);
  instance.ProcessEvents();

  bool mapSuccess = false;
  wgpu::Future mapFuture = readbackBuffer.MapAsync(
    wgpu::MapMode::Read, 0, bufferSize,
    wgpu::CallbackMode::WaitAnyOnly,
    [&mapSuccess](wgpu::MapAsyncStatus status, wgpu::StringView message) {
      if (status == wgpu::MapAsyncStatus::Success) {
        mapSuccess = true;
      } else {
        std::cerr << "Screenshot map failed: "
                  << std::string_view(message.data, message.length) << "\n";
      }
    });
  instance.WaitAny(mapFuture, UINT64_MAX);
  instance.ProcessEvents();

  if (!mapSuccess) {
    return false;
  }

  const void* mapped = readbackBuffer.GetConstMappedRange(0, bufferSize);
  if (!mapped) {
    readbackBuffer.Unmap();
    return false;
  }

  std::vector<uint8_t> rgb(width * height * 3);
  const uint8_t* srcBytes = static_cast<const uint8_t*>(mapped);
  for (uint32_t y = 0; y < height; ++y) {
    const uint8_t* row = srcBytes + static_cast<size_t>(y) * bytesPerRow;
    for (uint32_t x = 0; x < width; ++x) {
      const uint8_t* px = row + x * bytesPerPixel;
      uint8_t* out = &rgb[(y * width + x) * 3];
      if (format == wgpu::TextureFormat::BGRA8Unorm) {
        out[0] = px[2];
        out[1] = px[1];
        out[2] = px[0];
      } else {
        out[0] = px[0];
        out[1] = px[1];
        out[2] = px[2];
      }
    }
  }

  readbackBuffer.Unmap();

  std::error_code ec;
  std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);

  const int ok =
    stbi_write_png(path.c_str(), static_cast<int>(width), static_cast<int>(height), 3, rgb.data(),
                   static_cast<int>(width * 3));
  if (!ok) {
    std::cerr << "Screenshot: failed to write " << path << "\n";
    return false;
  }

  const std::filesystem::path absolutePath = std::filesystem::absolute(path);
  std::cout << "Screenshot saved: " << absolutePath.string() << "\n";
  return true;
}

} // namespace io
