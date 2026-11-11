#pragma once

#include "Core/Event.h"
#include "Core/Camera.h"

#include <glm/glm.hpp>

namespace Core {
	class MouseMovedEvent;
	class MouseScrolledEvent;
	class MouseButtonPressedEvent;
	class MouseButtonReleasedEvent;
	class KeyPressedEvent;
	class KeyReleasedEvent;
}

class EditorCameraController
{
public:
	EditorCameraController(Core::Camera& camera);

	void SyncOrientationFromCamera();

	void OnUpdate(float ts);
	void OnEvent(Core::Event& event);
private:
	bool OnMouseButtonPressed(Core::MouseButtonPressedEvent& event);
	bool OnMouseButtonReleased(Core::MouseButtonReleasedEvent& event);
	bool OnMouseMoved(Core::MouseMovedEvent& event);
	bool OnMouseScrolled(Core::MouseScrolledEvent& event);
	bool OnKeyPressed(Core::KeyPressedEvent& event);
	bool OnKeyReleased(Core::KeyReleasedEvent& event);

	void UpdateMovementState(int keyCode, bool isPressed);
private:
	Core::Camera& m_Camera;

	float m_Yaw = 0.0f;
	float m_Pitch = 0.0f;

	glm::vec2 m_LastMousePosition{ 0.0f, 0.0f };
	bool m_IsLooking = false;
	bool m_FirstMouse = true;

	float m_MovementSpeed = 3.0f;
	float m_RotationSpeed = 0.003f;

	bool m_MoveForward = false;
	bool m_MoveBackward = false;
	bool m_MoveLeft = false;
	bool m_MoveRight = false;
	bool m_MoveUp = false;
	bool m_MoveDown = false;
	bool m_MoveFast = false;
};
