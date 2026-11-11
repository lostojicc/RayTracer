#pragma once

#include "Core/Renderer/AABB.h"

#include <glm/glm.hpp>

namespace Benchmark {

	struct CameraFraming
	{
		glm::vec3 Position{ 0.0f };
		glm::vec3 Forward{ 0.0f, 0.0f, -1.0f };
		glm::vec3 Up{ 0.0f, 1.0f, 0.0f };
		float FocalLength = 1.0f;
	};

	// Computes a deterministic camera that frames 'bounds' entirely, from a
	// fixed viewing angle, regardless of the mesh's absolute scale.
	//
	// This matters for GPU BVH timing specifically: unlike brute-force
	// traversal (which always tests every triangle for every ray regardless
	// of visibility -- specs/gpu-integration.md "Brute-Force GPU Traversal"),
	// BVH traversal cost depends on which nodes rays actually pass near, so a
	// camera that does not actually see the model would silently under-test
	// the BVH path.
	CameraFraming ComputeFramingCamera(const Core::AABB& bounds);

}
