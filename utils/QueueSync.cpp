#include "utils/QueueSync.h"

#include <iostream>
#include <string_view>

namespace utils {

void WaitForQueue(const wgpu::Instance& instance, const wgpu::Queue& queue) {
  wgpu::Future future = queue.OnSubmittedWorkDone(
    wgpu::CallbackMode::WaitAnyOnly,
    [](wgpu::QueueWorkDoneStatus status, wgpu::StringView message) {
      if (status != wgpu::QueueWorkDoneStatus::Success) {
        std::cerr << "Queue work done error: "
                  << std::string_view(message.data, message.length) << "\n";
      }
    });
  instance.WaitAny(future, UINT64_MAX);
}

} // namespace utils
