#pragma once

#include <string>

#include <webgpu/webgpu_cpp.h>

namespace utils {

// Load a text file into a string (used for WGSL sources).
std::string LoadTextFile(const std::string& path);

// Load a WGSL shader file; logs size and returns empty on failure.
std::string LoadShaderFile(const std::string& path);

// Create a WGSL shader module from source code.
wgpu::ShaderModule CreateWgslModule(const wgpu::Device& device, const std::string& code);

} // namespace utils
