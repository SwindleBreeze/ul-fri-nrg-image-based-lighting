#pragma once

#include <webgpu/webgpu_cpp.h>

namespace utils {

void WaitForQueue(const wgpu::Instance& instance, const wgpu::Queue& queue);

} // namespace utils
