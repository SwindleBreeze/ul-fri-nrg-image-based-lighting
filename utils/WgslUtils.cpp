#include "utils/WgslUtils.h"

#include <fstream>
#include <iostream>
#include <sstream>

namespace utils {

std::string LoadTextFile(const std::string& path) {
  std::ifstream file(path, std::ios::in | std::ios::binary);
  if (!file) {
    return {};
  }

  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

std::string LoadShaderFile(const std::string& path) {
  const std::string code = LoadTextFile(path);
  if (code.empty()) {
    std::cerr << "Failed to load shader (missing or empty): " << path << "\n";
    return {};
  }

  std::cout << "Loaded shader: " << path << " (" << code.size() << " bytes)\n";
  return code;
}

wgpu::ShaderModule CreateWgslModule(const wgpu::Device& device, const std::string& code) {
  if (code.empty()) {
    std::cerr << "CreateWgslModule: empty shader source." << "\n";
    return {};
  }
  wgpu::ShaderSourceWGSL wgsl{};
  wgsl.code = wgpu::StringView(code.data(), code.size());

  wgpu::ShaderModuleDescriptor desc{};
  desc.nextInChain = &wgsl;

  return device.CreateShaderModule(&desc);
}

} // namespace utils
