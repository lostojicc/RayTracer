#pragma once

#include <glm/glm.hpp>

namespace Core {

	class Camera
	{
	public:
		Camera() = default;
		Camera(const glm::vec3& position, const glm::vec3& direction)
			: m_Position(position), m_ForwardDirection(direction) {}

		const glm::vec3& GetPosition() const { return m_Position; }
		const glm::vec3& GetDirection() const { return m_ForwardDirection; }
		const glm::vec3& GetUp() const { return m_Up; }
		float GetFocalLength() const { return m_FocalLength; }

		void SetPosition(const glm::vec3& position) { m_Position = position; m_Updated = true; }
		void SetDirection(const glm::vec3& direction) { m_ForwardDirection = direction; m_Updated = true; }
		void SetUp(const glm::vec3& up) { m_Up = up; m_Updated = true; }
		void SetFocalLength(float focalLength) { m_FocalLength = focalLength; m_Updated = true; }

		bool WasUpdated() const { return m_Updated; }
		void ClearUpdated() { m_Updated = false; }
	private:
		glm::vec3 m_Position{ 0.0f, 0.0f, 0.0f };
		glm::vec3 m_ForwardDirection{ 0.0f, 0.0f, -1.0f };
		glm::vec3 m_Up{ 0.0f, 1.0f, 0.0f };
		float m_FocalLength = 1.0f;

		bool m_Updated = false;
	};

}
