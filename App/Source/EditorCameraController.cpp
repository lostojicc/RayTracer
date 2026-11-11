#include "EditorCameraController.h"

#include "Core/Application.h"
#include "Core/InputEvents.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>

EditorCameraController::EditorCameraController(Core::Camera& camera)
	: m_Camera(camera)
{
	SyncOrientationFromCamera();
}

void EditorCameraController::SyncOrientationFromCamera()
{
	glm::vec3 direction = m_Camera.GetDirection();
	m_Pitch = std::asin(std::clamp(direction.y, -1.0f, 1.0f));
	m_Yaw = std::atan2(direction.x, -direction.z);
}

void EditorCameraController::OnUpdate(float ts)
{
	if (!m_IsLooking)
		return;

	glm::vec3 forward = m_Camera.GetDirection();
	glm::vec3 worldUp = m_Camera.GetUp();
	glm::vec3 right = glm::normalize(glm::cross(forward, worldUp));

	glm::vec3 velocity{ 0.0f };
	if (m_MoveForward)  velocity += forward;
	if (m_MoveBackward) velocity -= forward;
	if (m_MoveRight)    velocity += right;
	if (m_MoveLeft)     velocity -= right;
	if (m_MoveUp)       velocity += worldUp;
	if (m_MoveDown)     velocity -= worldUp;

	if (glm::dot(velocity, velocity) > 0.0f)
	{
		float speed = m_MovementSpeed * (m_MoveFast ? 3.0f : 1.0f);
		m_Camera.SetPosition(m_Camera.GetPosition() + glm::normalize(velocity) * speed * ts);
	}
}

void EditorCameraController::OnEvent(Core::Event& event)
{
	Core::EventDispatcher dispatcher(event);
	dispatcher.Dispatch<Core::MouseButtonPressedEvent>([this](Core::MouseButtonPressedEvent& e) { return OnMouseButtonPressed(e); });
	dispatcher.Dispatch<Core::MouseButtonReleasedEvent>([this](Core::MouseButtonReleasedEvent& e) { return OnMouseButtonReleased(e); });
	dispatcher.Dispatch<Core::MouseMovedEvent>([this](Core::MouseMovedEvent& e) { return OnMouseMoved(e); });
	dispatcher.Dispatch<Core::MouseScrolledEvent>([this](Core::MouseScrolledEvent& e) { return OnMouseScrolled(e); });
	dispatcher.Dispatch<Core::KeyPressedEvent>([this](Core::KeyPressedEvent& e) { return OnKeyPressed(e); });
	dispatcher.Dispatch<Core::KeyReleasedEvent>([this](Core::KeyReleasedEvent& e) { return OnKeyReleased(e); });
}

bool EditorCameraController::OnMouseButtonPressed(Core::MouseButtonPressedEvent& event)
{
	if (event.GetMouseButton() != GLFW_MOUSE_BUTTON_RIGHT)
		return false;

	m_IsLooking = true;
	m_FirstMouse = true;

	GLFWwindow* handle = Core::Application::Get().GetWindow()->GetHandle();
	glfwSetInputMode(handle, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

	return true;
}

bool EditorCameraController::OnMouseButtonReleased(Core::MouseButtonReleasedEvent& event)
{
	if (event.GetMouseButton() != GLFW_MOUSE_BUTTON_RIGHT)
		return false;

	m_IsLooking = false;

	GLFWwindow* handle = Core::Application::Get().GetWindow()->GetHandle();
	glfwSetInputMode(handle, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

	return true;
}

bool EditorCameraController::OnMouseMoved(Core::MouseMovedEvent& event)
{
	if (!m_IsLooking)
		return false;

	glm::vec2 position{ (float)event.GetX(), (float)event.GetY() };

	if (m_FirstMouse)
	{
		m_LastMousePosition = position;
		m_FirstMouse = false;
		return true;
	}

	glm::vec2 delta = position - m_LastMousePosition;
	m_LastMousePosition = position;

	if (delta.x == 0.0f && delta.y == 0.0f)
		return true;

	m_Yaw += delta.x * m_RotationSpeed;
	m_Pitch -= delta.y * m_RotationSpeed;

	float pitchLimit = glm::radians(89.0f);
	m_Pitch = std::clamp(m_Pitch, -pitchLimit, pitchLimit);

	glm::vec3 direction = glm::normalize(glm::vec3(
		std::cos(m_Pitch) * std::sin(m_Yaw),
		std::sin(m_Pitch),
		-std::cos(m_Pitch) * std::cos(m_Yaw)
	));
	m_Camera.SetDirection(direction);

	return true;
}

bool EditorCameraController::OnMouseScrolled(Core::MouseScrolledEvent& event)
{
	if (!m_IsLooking)
		return false;

	m_MovementSpeed = std::max(0.1f, m_MovementSpeed + (float)event.GetYOffset() * 0.5f);
	return true;
}

bool EditorCameraController::OnKeyPressed(Core::KeyPressedEvent& event)
{
	UpdateMovementState(event.GetKeyCode(), true);
	return false;
}

bool EditorCameraController::OnKeyReleased(Core::KeyReleasedEvent& event)
{
	UpdateMovementState(event.GetKeyCode(), false);
	return false;
}

void EditorCameraController::UpdateMovementState(int keyCode, bool isPressed)
{
	switch (keyCode)
	{
	case GLFW_KEY_W:          m_MoveForward = isPressed;  break;
	case GLFW_KEY_S:          m_MoveBackward = isPressed; break;
	case GLFW_KEY_A:          m_MoveLeft = isPressed;     break;
	case GLFW_KEY_D:          m_MoveRight = isPressed;    break;
	case GLFW_KEY_E:          m_MoveUp = isPressed;       break;
	case GLFW_KEY_Q:          m_MoveDown = isPressed;     break;
	case GLFW_KEY_LEFT_SHIFT: m_MoveFast = isPressed;     break;
	}
}
