#pragma once

#include "AABB.h"
#include "Ray.h"

#include <glm/glm.hpp>

namespace Core {

	// Möller-Trumbore determinant threshold. The determinant scales with
	// |ray.Direction| * |edge1| * |edge2|, so this tolerance is scale dependent:
	// it is tuned for normalized ray directions and unit-scale geometry.
	inline constexpr float TriangleIntersectionEpsilon = 1e-8f;

	// Threshold on |cross(edge1, edge2)|, which is twice the triangle area.
	inline constexpr float DegenerateAreaEpsilon = 1e-8f;

	// CPU triangle. Positions only, per specs/geometry.md.
	//
	// This is not the GPU upload representation. See specs/gpu-integration.md:
	// a GLSL vec3 is 16-byte aligned, so the GPU layout differs from this one
	// and requires an explicit serialization step.
	struct Triangle
	{
		glm::vec3 V0{ 0.0f };
		glm::vec3 V1{ 0.0f };
		glm::vec3 V2{ 0.0f };

		AABB Bounds() const;
		glm::vec3 Centroid() const;
		glm::vec3 Normal() const;

		bool IsDegenerate() const;

		bool Intersect(const Ray& ray, float tMin, float tMax, HitRecord& record) const;
	};

	inline AABB Triangle::Bounds() const
	{
		return AABB(
			glm::min(glm::min(V0, V1), V2),
			glm::max(glm::max(V0, V1), V2)
		);
	}

	inline glm::vec3 Triangle::Centroid() const
	{
		return (V0 + V1 + V2) / 3.0f;
	}

	inline glm::vec3 Triangle::Normal() const
	{
		const glm::vec3 normal = glm::cross(V1 - V0, V2 - V0);
		const float lengthSquared = glm::dot(normal, normal);

		// normalize() of a zero vector produces NaN; a degenerate triangle has
		// no meaningful normal, so report the zero vector instead.
		if (lengthSquared < DegenerateAreaEpsilon * DegenerateAreaEpsilon)
			return glm::vec3(0.0f);

		return normal / glm::sqrt(lengthSquared);
	}

	inline bool Triangle::IsDegenerate() const
	{
		const glm::vec3 normal = glm::cross(V1 - V0, V2 - V0);
		return glm::dot(normal, normal) < DegenerateAreaEpsilon * DegenerateAreaEpsilon;
	}

	// Möller-Trumbore, double sided. The tMin/tMax rejection matches hitSphere()
	// in App/Shaders/RayTracing.glsl so both primitives share one contract.
	inline bool Triangle::Intersect(const Ray& ray, float tMin, float tMax, HitRecord& record) const
	{
		const glm::vec3 edge1 = V1 - V0;
		const glm::vec3 edge2 = V2 - V0;

		const glm::vec3 pVec = glm::cross(ray.Direction, edge2);
		const float determinant = glm::dot(edge1, pVec);

		// One test rejects both rays parallel to the triangle plane and
		// degenerate triangles, since both drive the determinant to zero.
		if (glm::abs(determinant) < TriangleIntersectionEpsilon)
			return false;

		const float inverseDeterminant = 1.0f / determinant;

		const glm::vec3 tVec = ray.Origin - V0;
		const float u = glm::dot(tVec, pVec) * inverseDeterminant;
		if (u < 0.0f || u > 1.0f)
			return false;

		const glm::vec3 qVec = glm::cross(tVec, edge1);
		const float v = glm::dot(ray.Direction, qVec) * inverseDeterminant;
		if (v < 0.0f || u + v > 1.0f)
			return false;

		const float t = glm::dot(edge2, qVec) * inverseDeterminant;
		if (t <= tMin || t >= tMax)
			return false;

		const glm::vec3 outwardNormal = Normal();

		record.T = t;
		record.Position = ray.At(t);
		record.FrontFace = glm::dot(ray.Direction, outwardNormal) < 0.0f;
		record.Normal = record.FrontFace ? outwardNormal : -outwardNormal;
		record.U = u;
		record.V = v;

		return true;
	}

	static_assert(sizeof(Triangle) == 3 * sizeof(glm::vec3),
		"Core::Triangle is CPU-only and tightly packed (36 bytes). "
		"It does not match a GLSL triangle and must not be uploaded directly.");

}
