#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <limits>

namespace Core {

	inline constexpr uint32_t InvalidTriangleId = ~0u;

	// CPU mirror of the Ray/HitRecord pair in App/Shaders/RayTracing.glsl.
	// These are CPU-only types; they are never uploaded to a buffer.

	struct Ray
	{
		glm::vec3 Origin{ 0.0f };
		glm::vec3 Direction{ 0.0f, 0.0f, -1.0f };

		glm::vec3 At(float t) const { return Origin + t * Direction; }
	};

	struct HitRecord
	{
		glm::vec3 Position{ 0.0f };
		glm::vec3 Normal{ 0.0f };
		float T = 0.0f;
		bool FrontFace = false;

		// Barycentric coordinates relative to (V0, V1, V2).
		float U = 0.0f;
		float V = 0.0f;

		uint32_t TriangleId = InvalidTriangleId;
	};

}
