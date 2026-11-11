#pragma once

#include "Ray.h"

#include <glm/glm.hpp>

#include <limits>
#include <utility>

namespace Core {

	// Axis-aligned bounding box. CPU-only; contains no OpenGL or GLSL concepts.
	//
	// A default-constructed AABB is inverted/empty so that it acts as the
	// identity for Expand().
	struct AABB
	{
		glm::vec3 Min{ std::numeric_limits<float>::max() };
		glm::vec3 Max{ std::numeric_limits<float>::lowest() };

		AABB() = default;
		AABB(const glm::vec3& min, const glm::vec3& max)
			: Min(min), Max(max) {}

		bool IsEmpty() const
		{
			return Min.x > Max.x || Min.y > Max.y || Min.z > Max.z;
		}

		void Expand(const glm::vec3& point)
		{
			Min = glm::min(Min, point);
			Max = glm::max(Max, point);
		}

		void Expand(const AABB& other)
		{
			Min = glm::min(Min, other.Min);
			Max = glm::max(Max, other.Max);
		}

		bool Contains(const glm::vec3& point) const
		{
			return point.x >= Min.x && point.x <= Max.x
				&& point.y >= Min.y && point.y <= Max.y
				&& point.z >= Min.z && point.z <= Max.z;
		}

		bool Contains(const AABB& other) const
		{
			return other.IsEmpty() || (Contains(other.Min) && Contains(other.Max));
		}

		glm::vec3 Centroid() const { return (Min + Max) * 0.5f; }
		glm::vec3 Extent() const { return Max - Min; }

		bool Intersects(const Ray& ray, float tMin, float tMax, float* tEnter = nullptr) const;
	};

	// Slab test. Returns whether [tMin, tMax] overlaps the ray's interval inside
	// the box, and optionally the entry distance.
	inline bool AABB::Intersects(const Ray& ray, float tMin, float tMax, float* tEnter) const
	{
		for (int axis = 0; axis < 3; axis++)
		{
			const float inverseDirection = 1.0f / ray.Direction[axis];

			float t0 = (Min[axis] - ray.Origin[axis]) * inverseDirection;
			float t1 = (Max[axis] - ray.Origin[axis]) * inverseDirection;

			if (inverseDirection < 0.0f)
				std::swap(t0, t1);

			// A zero direction component with the origin exactly on a slab plane
			// produces 0 * inf = NaN. NaN fails both comparisons below, which
			// leaves the interval unchanged and treats the axis as "inside" --
			// the correct answer for that case.
			tMin = t0 > tMin ? t0 : tMin;
			tMax = t1 < tMax ? t1 : tMax;

			// Strict '<' keeps zero-thickness boxes (axis-aligned geometry)
			// intersectable. Grazing hits are reported, which is conservative
			// and therefore safe for an acceleration structure.
			if (tMax < tMin)
				return false;
		}

		if (tEnter)
			*tEnter = tMin;

		return true;
	}

	static_assert(sizeof(AABB) == 24, "Core::AABB is CPU-only and tightly packed.");

}
