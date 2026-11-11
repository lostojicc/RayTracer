#include "CameraFraming.h"

#include <cmath>

namespace Benchmark {

	CameraFraming ComputeFramingCamera(const Core::AABB& bounds)
	{
		CameraFraming framing;

		const glm::vec3 center = bounds.IsEmpty() ? glm::vec3(0.0f) : bounds.Centroid();
		const float boundingRadius = bounds.IsEmpty() ? 1.0f : (glm::length(bounds.Extent()) * 0.5f);
		const float safeRadius = std::max(boundingRadius, 0.05f);

		// A fixed, deterministic three-quarter viewing angle -- arbitrary but
		// not axis-aligned, so both traversal directions and node ordering
		// (specs/gpu-integration.md "Child Traversal Order") are exercised
		// similarly to a real scene view rather than a degenerate
		// straight-down-an-axis case.
		const glm::vec3 forward = glm::normalize(glm::vec3(-0.35f, -0.22f, -0.9f));
		const glm::vec3 up(0.0f, 1.0f, 0.0f);

		// Matches Core::Camera's default focal length (Core/Source/Core/Camera.h),
		// which is also what AppLayer's interactive scene uses unless the user
		// scrolls/zooms. tan(halfFovY) = 1 / focalLength here, mirroring
		// RayTracing.glsl's main(): 'ndc.y' is not rescaled by aspect, so the
		// vertical half-FOV alone determines whether the bounding sphere fits.
		const float focalLength = 1.0f;
		const float halfFovY = std::atan(1.0f / focalLength);

		// Margin factor (>1) so the model sits comfortably inside the frame
		// rather than exactly touching its edges.
		constexpr float kMarginFactor = 1.35f;
		const float distance = (safeRadius / std::sin(halfFovY)) * kMarginFactor;

		framing.Position = center - forward * distance;
		framing.Forward = forward;
		framing.Up = up;
		framing.FocalLength = focalLength;
		return framing;
	}

}
