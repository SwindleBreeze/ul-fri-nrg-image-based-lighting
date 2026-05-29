#pragma once

#include <glm/glm.hpp>

struct GLFWwindow;

namespace gfx {

struct OrbitCameraPose {
  glm::vec3 target{ 0.0f, 0.35f, 0.0f };
  float distance = 4.87f;
  float yaw = 0.8f;
  float pitch = -0.25f;
};

// Orbits around a target point. LMB drag to rotate, scroll to zoom.
class OrbitCamera {
public:
  void Attach(GLFWwindow* window);

  glm::vec3 GetPosition() const;
  glm::mat4 GetViewMatrix() const;
  glm::mat4 GetViewRotationMatrix() const;
  glm::mat4 GetProjection(float aspect) const;

  OrbitCameraPose GetPose() const;
  void SetPose(const OrbitCameraPose& pose);
  void LoadEvaluationPose();
  static OrbitCameraPose DefaultEvaluationPose();

private:
  void OnMouseButton(int button, int action);
  void OnCursorPos(double x, double y);
  void OnScroll(double yOffset);

  GLFWwindow* window_ = nullptr;
  glm::vec3 target_{ 0.0f, 0.35f, 0.0f };
  float distance_ = 4.87f;
  float yaw_ = 0.8f;
  float pitch_ = -0.25f;
  bool dragging_ = false;
  double lastCursorX_ = 0.0;
  double lastCursorY_ = 0.0;
};

} // namespace gfx
