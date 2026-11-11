#include "RayGeneration.h"

#include <random>

namespace Benchmark {

	std::vector<Core::Ray> GenerateTestRays(const Core::AABB& bounds, uint32_t rayCount, uint32_t seed)
	{
		std::vector<Core::Ray> rays;
		rays.reserve(rayCount);

		std::mt19937 rng(seed);
		std::uniform_real_distribution<float> signedUnit(-1.0f, 1.0f);
		std::uniform_real_distribution<float> jitter(-1.0f, 1.0f);

		const glm::vec3 center = bounds.IsEmpty() ? glm::vec3(0.0f) : bounds.Centroid();
		const glm::vec3 extentVec = bounds.IsEmpty() ? glm::vec3(1.0f) : bounds.Extent();
		const float enclosingRadius = bounds.IsEmpty() ? 1.0f : (glm::length(extentVec) * 0.75f + 1.0f);

		for (uint32_t i = 0; i < rayCount; i++)
		{
			// Uniform direction on the unit sphere via rejection sampling of a
			// point in [-1,1]^3 (rejecting only the near-zero-length case, an
			// astronomically unlikely event that would otherwise normalize
			// into a NaN/undefined direction).
			glm::vec3 direction;
			do
			{
				direction = glm::vec3(signedUnit(rng), signedUnit(rng), signedUnit(rng));
			} while (glm::dot(direction, direction) < 1e-6f);
			direction = glm::normalize(direction);

			const glm::vec3 origin = center - direction * enclosingRadius;

			// Aim at a jittered point inside (or, occasionally, just outside)
			// the bounds rather than exactly at the center every time, so both
			// hits and misses are exercised across the ray set.
			const glm::vec3 target = center + glm::vec3(
				jitter(rng) * extentVec.x * 0.6f,
				jitter(rng) * extentVec.y * 0.6f,
				jitter(rng) * extentVec.z * 0.6f);

			glm::vec3 aimDirection = target - origin;
			const float aimLengthSquared = glm::dot(aimDirection, aimDirection);
			aimDirection = aimLengthSquared > 1e-12f ? glm::normalize(aimDirection) : direction;

			Core::Ray ray;
			ray.Origin = origin;
			ray.Direction = aimDirection;
			rays.push_back(ray);
		}

		return rays;
	}

}
