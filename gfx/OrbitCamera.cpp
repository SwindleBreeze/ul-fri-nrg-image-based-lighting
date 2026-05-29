#include "gfx/OrbitCamera.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>

#include <algorithm>
#include <cmath>

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

namespace gfx {

namespace {

constexpr float kMinDistance = 0.75f;
constexpr float kMaxDistance = 50.0f;
constexpr float kMaxPitch = 1.55f;

} // namespace

void OrbitCamera::Attach(GLFWwindow* window) {
  window_ = window;
  glfwSetWindowUserPointer(window_, this);

  glfwSetMouseButtonCallback(window_, [](GLFWwindow* w, int button, int action, int mods) {
    ImGui_ImplGlfw_MouseButtonCallback(w, button, action, mods);
    if (auto* camera = static_cast<OrbitCamera*>(glfwGetWindowUserPointer(w))) {
      camera->OnMouseButton(button, action);
    }
  });

  glfwSetCursorPosCallback(window_, [](GLFWwindow* w, double x, double y) {
    ImGui_ImplGlfw_CursorPosCallback(w, x, y);
    if (auto* camera = static_cast<OrbitCamera*>(glfwGetWindowUserPointer(w))) {
      camera->OnCursorPos(x, y);
    }
  });

  glfwSetScrollCallback(window_, [](GLFWwindow* w, double xOffset, double yOffset) {
    ImGui_ImplGlfw_ScrollCallback(w, xOffset, yOffset);
    if (auto* camera = static_cast<OrbitCamera*>(glfwGetWindowUserPointer(w))) {
      camera->OnScroll(yOffset);
    }
  });

  glfwSetKeyCallback(window_, [](GLFWwindow* w, int key, int scancode, int action, int mods) {
    ImGui_ImplGlfw_KeyCallback(w, key, scancode, action, mods);
  });

  glfwSetCharCallback(window_, [](GLFWwindow* w, unsigned int c) {
    ImGui_ImplGlfw_CharCallback(w, c);
  });
}

glm::vec3 OrbitCamera::GetPosition() const {
  const float cosPitch = std::cos(pitch_);
  const glm::vec3 offset(
    distance_ * cosPitch * std::sin(yaw_),
    distance_ * std::sin(pitch_),
    distance_ * cosPitch * std::cos(yaw_));
  return target_ + offset;
}

glm::mat4 OrbitCamera::GetViewMatrix() const {
  return glm::lookAt(GetPosition(), target_, glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 OrbitCamera::GetViewRotationMatrix() const {
  glm::mat4 view = GetViewMatrix();
  view[3] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
  return view;
}

glm::mat4 OrbitCamera::GetProjection(float aspect) const {
  glm::mat4 proj = glm::perspectiveRH_ZO(glm::radians(45.0f), aspect, 0.1f, 200.0f);
  proj[1][1] *= -1.0f;
  return proj;
}

OrbitCameraPose OrbitCamera::GetPose() const {
  OrbitCameraPose pose;
  pose.target = target_;
  pose.distance = distance_;
  pose.yaw = yaw_;
  pose.pitch = pitch_;
  return pose;
}

void OrbitCamera::SetPose(const OrbitCameraPose& pose) {
  target_ = pose.target;
  distance_ = std::clamp(pose.distance, kMinDistance, kMaxDistance);
  yaw_ = pose.yaw;
  pitch_ = std::clamp(pose.pitch, -kMaxPitch, kMaxPitch);
}

OrbitCameraPose OrbitCamera::DefaultEvaluationPose() {
  return OrbitCameraPose{};
}

void OrbitCamera::LoadEvaluationPose() {
  SetPose(DefaultEvaluationPose());
}

void OrbitCamera::OnMouseButton(int button, int action) {
  if (ImGui::GetIO().WantCaptureMouse) {
    dragging_ = false;
    return;
  }
  if (button != GLFW_MOUSE_BUTTON_LEFT) {
    return;
  }

  if (action == GLFW_PRESS) {
    dragging_ = true;
    glfwGetCursorPos(window_, &lastCursorX_, &lastCursorY_);
  } else if (action == GLFW_RELEASE) {
    dragging_ = false;
  }
}

void OrbitCamera::OnCursorPos(double x, double y) {
  if (ImGui::GetIO().WantCaptureMouse || !dragging_) {
    return;
  }

  const double deltaX = x - lastCursorX_;
  const double deltaY = y - lastCursorY_;
  lastCursorX_ = x;
  lastCursorY_ = y;

  constexpr float kSensitivity = 0.005f;
  yaw_ += static_cast<float>(deltaX) * kSensitivity;
  pitch_ += static_cast<float>(deltaY) * kSensitivity;
  pitch_ = std::clamp(pitch_, -kMaxPitch, kMaxPitch);
}

void OrbitCamera::OnScroll(double yOffset) {
  if (ImGui::GetIO().WantCaptureMouse) {
    return;
  }
  distance_ -= static_cast<float>(yOffset) * 0.25f;
  distance_ = std::clamp(distance_, kMinDistance, kMaxDistance);
}

} // namespace gfx
