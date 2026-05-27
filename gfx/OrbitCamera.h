#pragma once

#include <glm/glm.hpp>

struct GLFWwindow;

namespace gfx {

// Orbits around a target point. LMB drag to rotate, scroll to zoom.
class OrbitCamera {
public:
  void Attach(GLFWwindow* window);

  glm::vec3 GetPosition() const;
  glm::mat4 GetViewMatrix() const;
  glm::mat4 GetViewRotationMatrix() const;
  glm::mat4 GetProjection(float aspect) const;

private:
  void OnMouseButton(int button, int action);
  void OnCursorPos(double x, double y);
  void OnScroll(double yOffset);

  GLFWwindow* window_ = nullptr;
  glm::vec3 target_{ 0.0f, 0.0f, 0.0f };
  float distance_ = 6.0f;
  float yaw_ = 0.0f;
  float pitch_ = 0.2f;
  bool dragging_ = false;
  double lastCursorX_ = 0.0;
  double lastCursorY_ = 0.0;
};

} // namespace gfx
